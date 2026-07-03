#include "barwindow.h"

#include "configreloader.h"
#include "wm/testwindowrules.h"
#include "qml/qbarpopupservice.h"
#include "qml/dockwindow.h"
#include "qml/qbaripc.h"
#include "qml/jsonasync.h"
#include "qml/jstimers.h"
#include "qml/netfetch.h"
#include "qml/jsprocess.h"
#include "qml/localstorage.h"
#include "qml/modelcapsules.h"
#include "qml/widgetreloader.h"
#include "json/jsonc.h"
#include "notifications/notificationserver.h"
#include "caffeine/caffeinemodel.h"
#include "platform/capslockmonitor.h"
#include "css/csstheme.h"
#include "dbus/dbusservice.h"
#include "platform/platformbarintegration.h"
#include "wm/wmbackendfactory.h"
#include "ipc/i3ipcclient.h"

#include <QApplication>
#include <QByteArray>
#include <QSet>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPoint>
#include <QDate>
#include <QProcess>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QPointF>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <unistd.h>

namespace {

class ThemeIconProvider final : public QQuickImageProvider {
public:
    ThemeIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int side = std::max(1, requestedSize.width() > 0 ? requestedSize.width() : 18);

        // '|' is not in the URL "unreserved" set, so QUrl percent-encodes it to
        // "%7C" and that escaped form survives into this id. Normalize both
        // spellings before splitting.
        QString normalizedId = id;
        normalizedId.replace(QStringLiteral("%7C"), QStringLiteral("|"), Qt::CaseInsensitive);
        const QStringList parts = normalizedId.split(QLatin1Char('|'));
        const QString iconId = parts.value(0);
        const QString iconThemePath = parts.size() > 2 ? parts.value(2) : QString();

        // Tint colors arrive as bare "RRGGBB" hex (without '#', which would be
        // parsed as a URL fragment and stripped before reaching this provider).
        QColor tintColor;
        if (parts.size() > 1 && !parts.value(1).isEmpty()) {
            QString tintSpec = parts.value(1);
            if (!tintSpec.startsWith(QLatin1Char('#'))) {
                tintSpec.prepend(QLatin1Char('#'));
            }
            tintColor = QColor(tintSpec);
        }

        QIcon icon;
        if (QFileInfo::exists(iconId)) {
            icon = QIcon(iconId);
        } else {
            icon = QIcon::fromTheme(iconId);
            if (icon.isNull()) {
                const QString fileName = findIconFile(iconId, iconThemePath);
                if (!fileName.isEmpty()) {
                    icon = QIcon(fileName);
                }
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }

        QPixmap pixmap = icon.pixmap(side, side);
        const QSize actualSize = pixmap.size();
        const int displaySide = std::min(side, std::max(actualSize.width(), actualSize.height()));
        if (displaySide < side) {
            pixmap = pixmap.scaled(displaySide, displaySide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (tintColor.isValid() && tintColor.alpha() > 0) {
            QPixmap tinted(displaySide, displaySide);
            tinted.fill(Qt::transparent);
            QPainter painter(&tinted);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawPixmap(0, 0, pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(tinted.rect(), tintColor);
            painter.end();
            pixmap = tinted;
        }

        if (size != nullptr) {
            *size = pixmap.size();
        }
        return pixmap;
    }

private:
    QString findIconFile(const QString &iconName, const QString &iconThemePath) const
    {
        QStringList iconNames;
        if (iconName.endsWith(QStringLiteral("-symbolic"))) {
            iconNames << iconName.left(iconName.size() - 9);
        }
        iconNames << iconName;

        QStringList names;
        for (const QString &name : iconNames) {
            names << name + QStringLiteral(".png")
                  << name + QStringLiteral(".svg")
                  << name + QStringLiteral(".xpm");
        }

        const QStringList themeNames = {
            QIcon::themeName(),
            QIcon::fallbackThemeName(),
            QStringLiteral("hicolor"),
        };

        auto pickLargest = [&](QDirIterator &it) -> QString {
            QString best;
            qint64 bestSize = 0;
            while (it.hasNext()) {
                const QString path = it.next();
                const qint64 sz = QFileInfo(path).size();
                if (sz > bestSize) {
                    best = path;
                    bestSize = sz;
                }
            }
            return best;
        };

        auto findInBases = [&](const QStringList &bases) -> QString {
            for (const QString &base : bases) {
                QDirIterator looseIterator(base, names, QDir::Files, QDirIterator::Subdirectories);
                const QString found = pickLargest(looseIterator);
                if (!found.isEmpty()) {
                    return found;
                }
            }
            for (const QString &theme : themeNames) {
                if (theme.isEmpty()) {
                    continue;
                }
                for (const QString &base : bases) {
                    const QString root = base + QLatin1Char('/') + theme;
                    if (!QFileInfo::exists(root)) {
                        continue;
                    }
                    QDirIterator iterator(root, names, QDir::Files, QDirIterator::Subdirectories);
                    const QString found = pickLargest(iterator);
                    if (!found.isEmpty()) {
                        return found;
                    }
                }
            }
            return {};
        };

        QStringList homeBases;
        QStringList systemBases;
        if (!iconThemePath.isEmpty()) {
            homeBases.prepend(iconThemePath);
            systemBases.prepend(iconThemePath);
        }
        for (const QString &base : QIcon::themeSearchPaths()) {
            if (base.startsWith(QDir::homePath())) {
                homeBases.append(base);
            } else {
                systemBases.append(base);
            }
        }

        const QString homeIcon = findInBases(homeBases);
        if (!homeIcon.isEmpty()) {
            return homeIcon;
        }

        const QString systemIcon = findInBases(systemBases);
        if (!systemIcon.isEmpty()) {
            return systemIcon;
        }

        return {};
    }
};

int clampCoordinate(int value, int minValue, int maxValue)
{
    return std::clamp(value, minValue, std::max(minValue, maxValue));
}

QPoint clampedPopupPosition(const QRect &screenArea, const QPoint &preferred, const QSize &popupSize)
{
    const int maxX = screenArea.x() + screenArea.width() - popupSize.width();
    const int maxY = screenArea.y() + screenArea.height() - popupSize.height();
    return QPoint(clampCoordinate(preferred.x(), screenArea.x(), maxX),
                  clampCoordinate(preferred.y(), screenArea.y(), maxY));
}

static QColor contrastColorFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2933"));
}

static QColor contrastGreenFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#86efac")) : QColor(QStringLiteral("#166534"));
}

QVariantMap themeMap(const BarConfig &config)
{
    return {
        {QStringLiteral("background"), config.background.name(QColor::HexArgb)},
        {QStringLiteral("foreground"), config.foreground.name(QColor::HexArgb)},
        {QStringLiteral("accent"), config.accent.name(QColor::HexArgb)},
        {QStringLiteral("accentForeground"), contrastColorFor(config.accent).name(QColor::HexArgb)},
        {QStringLiteral("contrastIcon"), contrastColorFor(config.background).name(QColor::HexArgb)},
        {QStringLiteral("eventDot"), contrastGreenFor(config.background).name(QColor::HexArgb)},
        {QStringLiteral("eventDotOnAccent"), contrastGreenFor(config.accent).name(QColor::HexArgb)},
        {QStringLiteral("fontFamily"), config.fontFamily},
        {QStringLiteral("fontSize"), config.fontSize},
        {QStringLiteral("trayItemPadding"), config.trayItemPadding},
        {QStringLiteral("animationDuration"), config.animationDuration},
        {QStringLiteral("animationEasing"), static_cast<int>(config.animationEasing.type())},
        {QStringLiteral("height"), config.height},
        {QStringLiteral("margin"), config.margin},
        {QStringLiteral("spacing"), config.spacing},
    };
}

// Default notification-toast styling, applied as a prelude (lowest priority) to
// whichever stylesheet the notifier uses — the bar's, or its own dedicated one
// (`notifications.styleSheet`). Every theme gets presentable frosted-glass toasts
// with entry/exit animations out of the box, and the theme's own `#notification*`
// rules override any of it. Translucent backgrounds on purpose — a compositor blur
// rule on the `qbar-notifications` layer namespace turns them into glass.
QString notificationCssPrelude()
{
    return QStringLiteral(
        "#notifications { width: 380px; margin: 14px; spacing: 10px; }\n"
        "@keyframes qbar-notif-in {\n"
        "  0%   { opacity: 0; transform: translateX(340px) scale(0.96); }\n"
        "  70%  { opacity: 1; transform: translateX(-6px); }\n"
        "  100% { opacity: 1; transform: translateX(0px); }\n"
        "}\n"
        "#notification {\n"
        "  background-color: rgba(40, 44, 58, 0.80);\n"
        "  border-color: rgba(255, 255, 255, 0.16);\n"
        "  border-width: 1px;\n"
        "  border-radius: 12px;\n"
        "  box-shadow: 0px 12px 28px rgba(0, 0, 0, 0.34);\n"
        "  padding: 12px;\n"
        "  animation: qbar-notif-in 320ms ease-out;\n"
        "}\n"
        "#notification:exit { transition: opacity 200ms ease-in; }\n"
        "#notification:hover { border-color: rgba(255, 255, 255, 0.30); }\n"
        "#notification:critical { background-color: rgba(96, 38, 48, 0.86); border-color: rgba(240, 110, 120, 0.85); }\n"
        "#notification:low { background-color: rgba(40, 44, 58, 0.62); }\n"
        "#notification.app { font-size: 10px; }\n"
        "#notification.summary { font-size: 12px; }\n"
        "#notification.body { font-size: 11px; }\n"
        "#notification.action { background-color: rgba(255, 255, 255, 0.10); border-radius: 6px; }\n"
        "#notification.action:hover { background-color: rgba(255, 255, 255, 0.20); }\n"
        "#notification.progress { background-color: rgba(255, 255, 255, 0.10); height: 3px; }\n"
        "#notification.value { background-color: rgba(255, 255, 255, 0.12); }\n");
}

} // namespace

BarWindow::BarWindow(const BarConfig &config, QWindow *parent)
    : QQuickView(parent)
    , m_config(config)
    , m_evolutionCalendarExecutable(QStandardPaths::findExecutable(QStringLiteral("evolution")))
{
    configureWindow();
    m_wm = createWindowManagerBackend(m_config.windowManagerBackend, this);
    m_capsLockMonitor = new CapsLockMonitor(this);
    m_cssTheme = new CssTheme(this);
    // Recompute the bar's CSS edge gap on every theme (re)load. CssTheme's own file watcher
    // reloads the stylesheet WITHOUT going through loadCssTheme(), so without this the margin
    // would only update on a full restart or theme swap, not on a live CSS edit.
    connect(m_cssTheme, &CssTheme::loadedChanged, this, &BarWindow::updateBarMarginsFromCss);
    m_configuredStyleSheet = m_config.styleSheet; // remembered so reset-css can revert a preview
    loadCssTheme();

    // Config hot-reload: the reloader watches/debounces/parses; applying is ours.
    auto *configReloader = new ConfigReloader(m_config.configFilePath, m_config.configIndex, this);
    connect(configReloader, &ConfigReloader::configReloaded, this, &BarWindow::applyReloadedConfig);

    // Custom-widget hot-reload: Bar.qml binds its widget Loaders to the reloader's
    // generation via the forwarded widgetReloadGeneration property below.
    m_widgetReloader = new WidgetReloader(engine(), m_config, this);
    connect(m_widgetReloader, &WidgetReloader::reloadGenerationChanged,
            this, &BarWindow::widgetReloadGenerationChanged);

    // X11/i3 fallback: WM rules that float/stick/pin the bar when there is no layer-shell.
    m_testWindowRules = new TestWindowRules(m_wm, m_config,
                                            [this]() { return targetBarGeometry(); }, this);
    if (auto *i3Backend = qobject_cast<I3IpcClient *>(m_wm)) {
        connect(i3Backend, SIGNAL(qbarNodeFound(qint64)),
                m_testWindowRules, SLOT(handleQbarNodeFound(qint64)));
    }
    buildLayout();
    positionAtTop();
    m_wm->start();
}

bool BarWindow::calendarAppAvailable() const
{
    return !m_evolutionCalendarExecutable.isEmpty();
}

int BarWindow::widgetReloadGeneration() const
{
    return m_widgetReloader != nullptr ? m_widgetReloader->reloadGeneration() : 0;
}

void BarWindow::closeEvent(QCloseEvent *event)
{
    qWarning() << "QBar closeEvent";
    QQuickView::closeEvent(event);
    QApplication::quit();
}

void BarWindow::hideEvent(QHideEvent *event)
{
    qWarning() << "QBar hideEvent";
    QQuickView::hideEvent(event);
}

void BarWindow::resizeEvent(QResizeEvent *event)
{
    QQuickView::resizeEvent(event);
}

void BarWindow::showEvent(QShowEvent *event)
{
    qWarning() << "QBar showEvent";
    QQuickView::showEvent(event);

    if (!m_platformIntegrationApplied) {
        applyPlatformBarIntegration(this, m_config);
        m_testWindowRules->schedule();
        m_platformIntegrationApplied = true;
    }
}

void BarWindow::loadCssTheme()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

    // Translate config-derived drawer `transition-duration`s into a CSS prelude that the
    // engine prepends (lowest priority) to the theme. This keeps the CSS the single source
    // of truth for the drawer animation: an explicit `#<name> { transition }` in the theme
    // overrides this default; otherwise the config value drives it.
    QString prelude;
    for (auto it = m_config.groups.constBegin(); it != m_config.groups.constEnd(); ++it) {
        const QVariantMap drawer = it.value().toMap().value(QStringLiteral("drawer")).toMap();
        if (!drawer.contains(QStringLiteral("transition-duration")))
            continue;
        QString name = it.key(); // "group/<name>"
        if (name.startsWith(QStringLiteral("group/")))
            name = name.mid(6);
        prelude += QStringLiteral("#%1 { transition: %2ms; }\n")
                       .arg(name)
                       .arg(drawer.value(QStringLiteral("transition-duration")).toInt());
    }
    // Default toast styling only matters here while the notifier shares this theme —
    // with a dedicated `notifications.styleSheet` the prelude lives on THAT theme
    // instead (see loadNotificationCssTheme).
    if (m_config.notifications.value(QStringLiteral("enabled"), false).toBool()
        && m_config.notifications.value(QStringLiteral("styleSheet")).toString().isEmpty()) {
        prelude += notificationCssPrelude();
    }
    m_cssTheme->setStylePrelude(prelude);

    // The CSS cascade, in order: an optional base (only if configured) that the theme
    // cascades over, then the theme itself (explicit styleSheet, else the conventional
    // qbar/style.css). loadLayered concatenates them so the theme wins on equal specificity.
    QStringList layer;
    if (!m_config.baseStyleSheet.isEmpty())
        layer.append(resolveThemeReference(m_config.baseStyleSheet));
    if (!m_config.styleSheet.isEmpty())
        layer.append(resolveThemeReference(m_config.styleSheet));
    else
        layer.append(QDir(configDir).filePath(QStringLiteral("qbar/style.css")));

    m_cssTheme->loadLayered(layer);
    // loadLayered emits loadedChanged → updateBarMarginsFromCss(), so the bar margins are
    // already current here; no explicit call needed.
}

// Resolve a theme reference: http(s)/file URLs and absolute paths pass through; a bare
// relative path resolves against the directory of THIS config file (so it works the same
// whether the config is at ~/.config/qbar/ or wherever --config points). Lets the bundled
// default config portably say "themes/nord.css".
QString BarWindow::resolveThemeReference(const QString &pathOrUrl) const
{
    if (pathOrUrl.isEmpty())
        return pathOrUrl;
    const QString scheme = QUrl(pathOrUrl).scheme();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("file") || QDir::isAbsolutePath(pathOrUrl)) {
        return pathOrUrl;
    }
    const QString configBaseDir = !m_config.configFilePath.isEmpty()
        ? QFileInfo(m_config.configFilePath).absolutePath()
        : QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
              .filePath(QStringLiteral("qbar"));
    return QDir(configBaseDir).filePath(pathOrUrl);
}

void BarWindow::loadNotificationCssTheme()
{
    if (m_notificationCssTheme == nullptr) {
        return;
    }
    const QString sheet = m_config.notifications.value(QStringLiteral("styleSheet")).toString();
    if (sheet.isEmpty()) {
        // The key was removed at runtime; keep the last dedicated theme (the daemon's
        // window can't be re-pointed at the bar's CssTheme without a restart).
        qWarning() << "qbar: notifications.styleSheet removed — restart qbar to re-share the bar theme";
        return;
    }
    m_notificationCssTheme->setStylePrelude(notificationCssPrelude());
    m_notificationCssTheme->loadLayered({resolveThemeReference(sheet)});
}

void BarWindow::updateBarMarginsFromCss()
{
    if (m_cssTheme == nullptr) {
        return;
    }
    // Bar edge gap from the bar's CSS (#qbar / window#waybar are synonyms here) — margin-top /
    // margin-bottom, or the `margin` shorthand (waybar floating-bar idiom). The layer-shell
    // integration reads these window properties. CSS-ONLY on purpose: the JSON `margin` has
    // never driven the layer gap, so honouring it now would shift every existing config.
    // Horizontal insets stay with Bar.qml (margin-left/right → content inset).
    QVariantMap barRule = m_cssTheme->resolve(QStringLiteral("waybar"));
    const QVariantMap qbarRule = m_cssTheme->resolveExact(QStringLiteral("qbar"));
    for (auto it = qbarRule.constBegin(); it != qbarRule.constEnd(); ++it) {
        barRule.insert(it.key(), it.value());
    }
    int shorthandTop = 0;
    int shorthandBottom = 0;
    bool hasShorthand = false;
    if (barRule.contains(QStringLiteral("margin"))) {
        const QStringList parts = barRule.value(QStringLiteral("margin")).toString().trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            hasShorthand = true;
            shorthandTop = qRound(m_cssTheme->parseLength(parts.first(), 0));
            shorthandBottom = parts.size() >= 3 ? qRound(m_cssTheme->parseLength(parts.at(2), 0)) : shorthandTop;
        }
    }
    const auto verticalMargin = [&](const char *key, int shorthandVal) -> int {
        if (barRule.contains(QLatin1String(key))) {
            return qRound(m_cssTheme->parseLength(barRule.value(QLatin1String(key)).toString(), 0));
        }
        return hasShorthand ? shorthandVal : 0;
    };
    setProperty("qbarBarMarginTop", verticalMargin("margin-top", shorthandTop));
    setProperty("qbarBarMarginBottom", verticalMargin("margin-bottom", shorthandBottom));
    qInfo("QBar bar edge margins from CSS: top=%lld bottom=%lld",
          property("qbarBarMarginTop").toLongLong(), property("qbarBarMarginBottom").toLongLong());
}

bool BarWindow::setStyleSheet(const QString &pathOrUrl)
{
    if (pathOrUrl.isEmpty() || m_cssTheme == nullptr) {
        return false;
    }

    // Treat the argument as a URL when applicable: http(s) → fetch and apply the CSS body;
    // file:// or a bare path → load from disk (the default).
    const QUrl url(pathOrUrl);
    const QString scheme = url.scheme();

    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (m_cssNam == nullptr) {
            m_cssNam = new QNetworkAccessManager(this);
        }
        QNetworkRequest request(url);
        request.setTransferTimeout(std::chrono::seconds(15));
        QNetworkReply *reply = m_cssNam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "QBar set-css: fetch failed:" << reply->errorString();
                return;
            }
            // Resolve @import relative to the fetched URL (so a web theme's relative
            // imports load from the web too), then apply the inlined CSS.
            auto visited = QSharedPointer<QStringList>::create();
            visited->append(url.toString(QUrl::RemoveFragment));
            resolveCssImports(QString::fromUtf8(reply->readAll()), url, visited,
                              [this](const QString &css) { m_cssTheme->loadFromString(css); });
        });
        return true; // request dispatched; the theme applies when it arrives
    }

    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!QFileInfo::exists(path)) {
        qWarning() << "QBar set-css: file not found:" << path;
        return false;
    }
    m_config.styleSheet = path; // so a later reload keeps the swapped theme
    loadCssTheme();             // re-layer: keeps baseStyleSheet (if any) under the new theme
    return true;
}

bool BarWindow::resetStyleSheet()
{
    if (m_cssTheme == nullptr) {
        return false;
    }
    m_config.styleSheet = m_configuredStyleSheet; // revert a set-css preview to the configured theme
    loadCssTheme();
    return true;
}

void BarWindow::resolveCssImports(const QString &css, const QUrl &base,
                                  QSharedPointer<QStringList> visited,
                                  std::function<void(const QString &)> done)
{
    static const QRegularExpression importRe(
        QStringLiteral(R"(@import\s+(?:url\(\s*)?['"]?([^'")\s]+)['"]?\s*\)?\s*;)"));
    const QRegularExpressionMatch m = importRe.match(css);
    if (!m.hasMatch()) {
        done(css);
        return;
    }

    const QString before = css.left(m.capturedStart());
    const QString after = css.mid(m.capturedEnd());
    // CSS @import resolves relative to the importing document's URL — so a relative ref in
    // a web theme stays on the web, and a relative ref in a file theme stays on disk.
    const QUrl target = base.resolved(QUrl(m.captured(1)));
    const QString key = target.toString(QUrl::RemoveFragment);

    // Splice the imported content (after expanding ITS own imports, relative to ITS url)
    // in place of the @import, then continue resolving the rest of the parent document.
    const auto onContent = [this, before, after, target, base, visited, done](const QString &sub) {
        resolveCssImports(sub, target, visited,
                          [this, before, after, base, visited, done](const QString &expanded) {
                              resolveCssImports(before + expanded + after, base, visited, done);
                          });
    };

    if (visited->contains(key)) { // already imported (cycle / duplicate) — drop it
        resolveCssImports(before + after, base, visited, done);
        return;
    }
    visited->append(key);

    const QString scheme = target.scheme();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (m_cssNam == nullptr) {
            m_cssNam = new QNetworkAccessManager(this);
        }
        QNetworkRequest request(target);
        request.setTransferTimeout(std::chrono::seconds(15));
        QNetworkReply *reply = m_cssNam->get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, onContent]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "QBar @import: fetch failed:" << reply->errorString();
            }
            onContent(reply->error() == QNetworkReply::NoError
                          ? QString::fromUtf8(reply->readAll()) : QString());
        });
    } else {
        const QString local = target.isLocalFile() ? target.toLocalFile() : target.path();
        QFile file(local);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "QBar @import: cannot read" << local;
            onContent(QString());
            return;
        }
        onContent(QString::fromUtf8(file.readAll()));
    }
}

void BarWindow::applyReloadedConfig(const BarConfig &fresh)
{
    if (fresh.styleSheet != m_config.styleSheet || fresh.baseStyleSheet != m_config.baseStyleSheet) {
        m_config.styleSheet = fresh.styleSheet;
        m_configuredStyleSheet = fresh.styleSheet; // keep reset-css pointed at the config's theme
        m_config.baseStyleSheet = fresh.baseStyleSheet;
        loadCssTheme();
    }

    if (fresh.customTools != m_config.customTools) {
        m_config.customTools = fresh.customTools;
        rootContext()->setContextProperty(QStringLiteral("customTools"), m_config.customTools);
    }

    // Dock options (magnify/indicator/heights): forward to the live dock so a
    // config edit re-styles it without a restart.
    if (fresh.dock != m_config.dock) {
        m_config.dock = fresh.dock;
        if (m_dockWindow != nullptr) {
            m_dockWindow->setDockConfig(m_config.dock);
        }
    }

    // Notification options (corner/timeouts/maxVisible): forward to the daemon.
    // (enabled cannot be toggled live — the bus name registration is per-process.)
    if (fresh.notifications != m_config.notifications) {
        const QString oldSheet =
            m_config.notifications.value(QStringLiteral("styleSheet")).toString();
        m_config.notifications = fresh.notifications;
        if (NotificationServer::instance() != nullptr) {
            NotificationServer::instance()->setConfig(m_config.notifications);
        }
        const QString newSheet =
            m_config.notifications.value(QStringLiteral("styleSheet")).toString();
        if (newSheet != oldSheet) {
            if (m_notificationCssTheme != nullptr) {
                loadNotificationCssTheme(); // re-point the dedicated theme at the new file
            } else if (!newSheet.isEmpty()) {
                qWarning() << "qbar: notifications.styleSheet added — restart qbar to apply "
                              "(the daemon was created sharing the bar theme)";
            }
        }
    }

    // Module/group changes: update the applet lists the Bar.qml Repeaters bind to (and expose
    // any newly-configured applet's backend), so adding/removing an applet takes effect live.
    if (fresh.appletsLeft != m_config.appletsLeft || fresh.appletsCenter != m_config.appletsCenter
        || fresh.appletsRight != m_config.appletsRight || fresh.groups != m_config.groups) {
        m_config.appletsLeft = fresh.appletsLeft;
        m_config.appletsCenter = fresh.appletsCenter;
        m_config.appletsRight = fresh.appletsRight;
        m_config.applets = fresh.applets;
        m_config.groups = fresh.groups;
        exposeModels();
        rootContext()->setContextProperty(QStringLiteral("appletNames"), m_config.applets);
        rootContext()->setContextProperty(QStringLiteral("leftApplets"), m_config.appletsLeft);
        rootContext()->setContextProperty(QStringLiteral("centerApplets"), m_config.appletsCenter);
        rootContext()->setContextProperty(QStringLiteral("rightApplets"), m_config.appletsRight);
        rootContext()->setContextProperty(QStringLiteral("barGroups"), m_config.groups);
    }
}

void BarWindow::configureWindow()
{
    setTitle(QStringLiteral("QBar"));
    setProperty("appId", QStringLiteral("qbar"));
    setProperty("desktopFileName", QStringLiteral("qbar"));
    // Per-window layer geometry so multiple bars (multi-monitor, top+bottom) can
    // differ; the layer-shell integration reads these, falling back to env.
    setProperty("qbarBarHeight", m_config.height);
    setProperty("qbarBarPosition", barPositionName(m_config.position));
    setProperty("qbarBarExclusive", m_config.exclusiveZone);
    // (Bar edge margins are derived from CSS in loadCssTheme(), once m_cssTheme exists.)
    // Honour the configured height on every backend. The x11 dock reserves
    // exactly config.height via _NET_WM_STRUT, so forcing a 50px floor here only
    // made the window taller than its reserved strut — leaving the applets
    // floating in an oversized bar. Match the window to the configured height.
    const int windowHeight = m_config.height;
    setMinimumHeight(windowHeight);
    setMaximumHeight(windowHeight);
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (!m_config.waylandLayerShell) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setFlags(flags);
    setColor(Qt::transparent);
}

// Expose the lazy capsule backends as context properties, but ONLY for the applets this bar's
// config actually uses — so an unconfigured applet's backend is never acquired (never built).
// acquire() is a shared process-wide singleton, so a model used on more than one bar is built
// once. To QML each is an ordinary context property — it can't tell eager from lazy. Safe to
// re-run on a live config reload: acquire() returns the cached model, and newly-configured
// applets get their model exposed (so the Repeaters that just gained them find it).
void BarWindow::exposeModels()
{
    QSet<QString> usedApplets;
    const auto addModule = [&usedApplets](const QString &token) {
        if (token.startsWith(QLatin1String("CustomTool:")) || token.startsWith(QLatin1String("group/")))
            return;
        const int slash = token.indexOf(QLatin1Char('/')); // strip a "/variant" suffix
        usedApplets.insert(slash >= 0 ? token.left(slash) : token);
    };
    for (const QString &t : m_config.appletsLeft) {
        addModule(t);
    }
    for (const QString &t : m_config.appletsCenter) {
        addModule(t);
    }
    for (const QString &t : m_config.appletsRight) {
        addModule(t);
    }
    for (auto it = m_config.groups.constBegin(); it != m_config.groups.constEnd(); ++it) {
        const QVariantList mods = it.value().toMap().value(QStringLiteral("modules")).toList();
        for (const QVariant &m : mods) {
            addModule(m.toString());
        }
    }
    const auto expose = [this, &usedApplets](const char *applet, const char *ctx, const char *key) {
        if (usedApplets.contains(QLatin1String(applet))) {
            rootContext()->setContextProperty(QLatin1String(ctx),
                                               ModelCapsules::instance()->acquire(QLatin1String(key), this));
        }
    };
    expose("CPU", "cpuModel", "cpu");
    expose("Memory", "cpuModel", "cpu");          // Memory reads cpuModel too
    expose("Temperature", "temperatureModel", "temperature");
    expose("Network", "networkModel", "network");
    expose("Network", "networkProcessModel", "networkProcess");
    expose("NetworkManager", "networkManagerModel", "networkManager");
    expose("Brightness", "brightnessModel", "brightness");
    expose("Media", "mprisModel", "mpris");
    expose("Battery", "batteryModel", "battery");
    expose("Tray", "trayModel", "tray");
    expose("Sound", "soundModel", "sound");
    expose("Caffeine", "caffeineModel", "caffeine");
    expose("Disk", "diskModel", "disk");
    expose("Bluetooth", "bluetoothModel", "bluetooth");
    expose("PowerProfiles", "powerProfilesModel", "powerProfiles");
    expose("UPower", "upowerModel", "upower");
    expose("User", "userModel", "user");
    expose("Privacy", "privacyModel", "privacy");
    expose("Clock", "calendarModel", "calendar"); // the calendar popup Clock opens
}

void BarWindow::buildLayout()
{
    const QVariantMap theme = themeMap(m_config);
    if (engine()->imageProvider(QStringLiteral("themeicon")) == nullptr) {
        engine()->addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    }

    // Web-style setTimeout/setInterval globals (QTimer-backed) — Qt's QML engine has none.
    JsTimers::install(engine());
    // `Http` global — a QNetworkAccessManager-backed fetch transport for Fetch.js, since
    // QML's XMLHttpRequest silently hangs on network failure (no timeout/onerror).
    NetFetch::install(engine());
    // `JsonAsync` global — worker-threaded JSON.parse for Json.js, so large payloads
    // don't block the event loop and stutter animations (the MPRIS marquee).
    JsonAsync::install(engine());
    // Persistent Web-Storage-like key/value API for runtime QML widgets.
    LocalStorage::install(engine());
    // `Proc` global — a QProcess-backed runner so custom QML widgets can shell out to
    // external commands (the QML side has no process API). Async + self-cleaning like Http.
    JsProcess::install(engine());

    m_popupService = new QBarPopupService(engine(), theme, m_wm->workspaceModel(), m_wm, m_cssTheme, this);
    m_popupService->setOverlayKeyboardFocus(m_config.popupKeyboardFocus);
    m_popupService->setBarWindow(this);
    connect(m_popupService, &QBarPopupService::popupClosed, this, [this](const QString &id) {
        if (id == m_calendarPopupId) {
            m_calendarPopupId.clear();
        }
    });
    connect(m_wm, &WindowManagerBackend::workspaceFocusEvent, m_popupService, &QBarPopupService::closeAll);

    // The macOS-style Dock controller. Cheap to construct and creates no window until
    // the in-bar "Dock" proxy applet first reports a non-empty slot, so bars without a
    // Dock pay nothing. Exposed to QML as `dockController`.
    m_dockWindow = new DockWindow(engine(), theme, m_config.dock, m_wm->windowModel(), m_wm, m_cssTheme, this);
    m_dockWindow->setBarWindow(this);

    // The notification daemon (org.freedesktop.Notifications → CSS-themed toasts).
    // ONE per process — only the first bar that enables it creates the server (the
    // bus name can only be owned once anyway); later bars just reuse it.
    if (m_config.notifications.value(QStringLiteral("enabled"), false).toBool()
        && NotificationServer::instance() == nullptr) {
        // `notifications.styleSheet` gives the toasts their OWN stylesheet (own
        // CssTheme, own file watcher/hot-reload), independent of the bar's theme —
        // the same separation the lock's *-lock.css files have. Unset = share the
        // bar's theme, whose `#notification*` rules (or the prelude defaults) apply.
        QObject *notificationTheme = m_cssTheme;
        if (!m_config.notifications.value(QStringLiteral("styleSheet")).toString().isEmpty()) {
            m_notificationCssTheme = new CssTheme(this);
            loadNotificationCssTheme();
            notificationTheme = m_notificationCssTheme;
        }
        auto *notifications =
            new NotificationServer(engine(), theme, m_config.notifications, notificationTheme, this);
        notifications->setBarWindow(this);
    }

    rootContext()->setContextProperty(QStringLiteral("theme"), theme);
    // Directory of the active config file — relative custom-widget `source` paths
    // (runtime QML loaded by Bar.appletUrl) resolve against it.
    rootContext()->setContextProperty(QStringLiteral("configDir"),
                                      m_config.configFilePath.isEmpty()
                                          ? QString()
                                          : QFileInfo(m_config.configFilePath).absolutePath());
    rootContext()->setContextProperty(QStringLiteral("popupEmbossShaderAvailable"),
#ifdef QBAR_HAVE_POPUP_EMBOSS_SHADER
                                      true
#else
                                      false
#endif
    );
    rootContext()->setContextProperty(QStringLiteral("qbarPopups"), m_popupService);
    rootContext()->setContextProperty(QStringLiteral("qbarIpc"), QbarIpc::instance());
    QbarIpc::instance()->registerBar(this); // enables the IPC `set-css` command
    rootContext()->setContextProperty(QStringLiteral("workspaceModel"), m_wm->workspaceModel());
    rootContext()->setContextProperty(QStringLiteral("windowModel"), m_wm->windowModel());
    rootContext()->setContextProperty(QStringLiteral("dockController"), m_dockWindow);
    rootContext()->setContextProperty(QStringLiteral("taskbarConfig"), m_config.taskbar);
    rootContext()->setContextProperty(QStringLiteral("cpuConfig"), m_config.cpu);
    rootContext()->setContextProperty(QStringLiteral("memoryConfig"), m_config.memory);
    rootContext()->setContextProperty(QStringLiteral("networkConfig"), m_config.network);
    exposeModels(); // lazy capsule backends, gated by which applets this bar's config uses

    rootContext()->setContextProperty(QStringLiteral("capsLock"), m_capsLockMonitor);
    rootContext()->setContextProperty(QStringLiteral("wm"), m_wm);
    rootContext()->setContextProperty(QStringLiteral("i3Ipc"), m_wm);
    rootContext()->setContextProperty(QStringLiteral("cssTheme"), m_cssTheme);
    rootContext()->setContextProperty(QStringLiteral("dbus"), new DBusService(engine(), this));
    rootContext()->setContextProperty(QStringLiteral("customTools"), m_config.customTools);
    rootContext()->setContextProperty(QStringLiteral("appletNames"), m_config.applets);
    rootContext()->setContextProperty(QStringLiteral("leftApplets"), m_config.appletsLeft);
    rootContext()->setContextProperty(QStringLiteral("centerApplets"), m_config.appletsCenter);
    rootContext()->setContextProperty(QStringLiteral("rightApplets"), m_config.appletsRight);
    rootContext()->setContextProperty(QStringLiteral("barGroups"), m_config.groups);
    rootContext()->setContextProperty(QStringLiteral("barWindow"), this);

    setResizeMode(QQuickView::SizeRootObjectToView);
    setSource(QUrl(QStringLiteral("qrc:/Bar.qml")));
}

void BarWindow::positionAtTop()
{
    const QRect target = targetBarGeometry();
    setMinimumSize(target.size());
    setMaximumSize(target.size());
    setGeometry(target);
}

QRect BarWindow::targetBarGeometry() const
{
    const auto *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return QRect(0, 0, 900, m_config.height);
    }

    const auto area = m_config.waylandLayerShell ? screen->availableGeometry() : screen->geometry();
    const int targetHeight = m_config.height;
    const int y = m_config.position == BarPosition::Bottom
        ? area.y() + area.height() - targetHeight
        : area.y();
    const int targetX = m_config.x >= 0 ? m_config.x : area.x();
    const int targetY = m_config.y >= 0 ? m_config.y : y;
    return QRect(targetX, targetY, area.width(), targetHeight);
}

void BarWindow::openCalendar(QObject *anchorObject)
{
    if (m_popupService == nullptr) {
        return;
    }

    if (!m_calendarPopupId.isEmpty()) {
        m_popupService->closePopup(m_calendarPopupId);
        m_calendarPopupId.clear();
        return;
    }

    const int popupWidth = 560;
    const int popupHeight = 380;
    const QSize popupSize(popupWidth, popupHeight);
    const int gap = 0;
    auto *anchorItem = qobject_cast<QQuickItem *>(anchorObject);
    QPoint anchorTopLeft = mapToGlobal(QPoint(0, 0));
    QPoint anchorBottomRight = mapToGlobal(QPoint(width(), height()));

    if (anchorItem != nullptr) {
        const QPointF itemTopLeft = anchorItem->mapToScene(QPointF(0.0, 0.0));
        const QPointF itemBottomRight = anchorItem->mapToScene(QPointF(anchorItem->width(), anchorItem->height()));
        anchorTopLeft = mapToGlobal(QPoint(qRound(itemTopLeft.x()), qRound(itemTopLeft.y())));
        anchorBottomRight = mapToGlobal(QPoint(qRound(itemBottomRight.x()), qRound(itemBottomRight.y())));
    }

    int x = anchorBottomRight.x() - popupWidth;
    int y = m_config.position == BarPosition::Bottom
        ? anchorTopLeft.y() - popupHeight - gap
        : anchorBottomRight.y() + gap;

    const QScreen *screen = QGuiApplication::screenAt(anchorTopLeft);
    if (screen != nullptr) {
        const QPoint clamped = clampedPopupPosition(screen->availableGeometry(), QPoint(x, y), popupSize);
        x = clamped.x();
        y = clamped.y();
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("selectedDate"), QDate::currentDate());
    m_calendarPopupId = m_popupService->openPopup(QUrl(QStringLiteral("qrc:/popups/CalendarPopup.qml")),
                                                  properties,
                                                  x,
                                                  y,
                                                  popupWidth,
                                                  popupHeight,
                                                  QStringLiteral("calendar"));
    if (m_calendarPopupId.isEmpty()) {
        qWarning() << "QBar calendar popup failed to open";
    }
}

void BarWindow::openEvolutionCalendar()
{
    if (!calendarAppAvailable()) {
        return;
    }

    const QStringList arguments = {QStringLiteral("--component=calendar")};
    if (!QProcess::startDetached(m_evolutionCalendarExecutable, arguments)) {
        qWarning() << "QBar failed to launch Evolution Calendar" << m_evolutionCalendarExecutable << arguments;
    }
}

void BarWindow::cycleKeyboardLayout()
{
    if (m_wm != nullptr) {
        m_wm->cycleKeyboardLayout();
    }
}

void BarWindow::toggleCaffeine()
{
    auto *model = qobject_cast<CaffeineModel *>(
        ModelCapsules::instance()->acquire(QStringLiteral("caffeine"), this));
    if (model != nullptr) {
        model->toggle();
    }
}
