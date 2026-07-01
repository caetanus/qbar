#include "barwindow.h"

#include "qml/qbarpopupservice.h"
#include "qml/dockwindow.h"
#include "qml/qbaripc.h"
#include "qml/jsonasync.h"
#include "qml/jstimers.h"
#include "qml/netfetch.h"
#include "qml/jsprocess.h"
#include "qml/localstorage.h"
#include "qml/modelcapsules.h"
#include "json/jsonc.h"
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

    m_configWatcher = new QFileSystemWatcher(this);
    if (!m_config.configFilePath.isEmpty() && QFileInfo::exists(m_config.configFilePath)) {
        QFile f(m_config.configFilePath);
        if (f.open(QIODevice::ReadOnly)) {
            m_configHash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        }
        m_configWatcher->addPath(m_config.configFilePath);
    }
    connect(m_configWatcher, &QFileSystemWatcher::fileChanged, this, &BarWindow::onConfigFileChanged);

    // Debounce reloads: editors save by truncating or atomically replacing the file, so the
    // watcher often fires mid-write. Wait a beat so we read the SETTLED file, not a half-written
    // one (which parsed as "invalid JSONC at offset 0" and dropped the reload).
    m_configReloadTimer = new QTimer(this);
    m_configReloadTimer->setSingleShot(true);
    m_configReloadTimer->setInterval(120);
    connect(m_configReloadTimer, &QTimer::timeout, this, &BarWindow::reloadConfigFromDisk);

    setupWidgetWatcher();

    if (auto *i3Backend = qobject_cast<I3IpcClient *>(m_wm)) {
        connect(i3Backend, SIGNAL(qbarNodeFound(qint64)), this, SLOT(handleQbarNodeFound(qint64)));
    }
    buildLayout();
    positionAtTop();
    m_wm->start();
}

bool BarWindow::calendarAppAvailable() const
{
    return !m_evolutionCalendarExecutable.isEmpty();
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
        scheduleTestWindowRules();
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
    m_cssTheme->setStylePrelude(prelude);

    // The CSS cascade, in order: an optional base (only if configured) that the theme
    // cascades over, then the theme itself (explicit styleSheet, else the conventional
    // qbar/style.css). loadLayered concatenates them so the theme wins on equal specificity.
    // Resolve a theme reference: http(s)/file URLs and absolute paths pass through; a bare
    // relative path resolves against the directory of THIS config file (so it works the same
    // whether the config is at ~/.config/qbar/ or wherever --config points). Lets the bundled
    // default config portably say "themes/nord.css".
    const QString configBaseDir = !m_config.configFilePath.isEmpty()
        ? QFileInfo(m_config.configFilePath).absolutePath()
        : QDir(configDir).filePath(QStringLiteral("qbar"));
    const auto resolveTheme = [&configBaseDir](const QString &p) -> QString {
        if (p.isEmpty())
            return p;
        const QString scheme = QUrl(p).scheme();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
            || scheme == QLatin1String("file") || QDir::isAbsolutePath(p)) {
            return p;
        }
        return QDir(configBaseDir).filePath(p);
    };

    QStringList layer;
    if (!m_config.baseStyleSheet.isEmpty())
        layer.append(resolveTheme(m_config.baseStyleSheet));
    if (!m_config.styleSheet.isEmpty())
        layer.append(resolveTheme(m_config.styleSheet));
    else
        layer.append(QDir(configDir).filePath(QStringLiteral("qbar/style.css")));

    m_cssTheme->loadLayered(layer);
    // loadLayered emits loadedChanged → updateBarMarginsFromCss(), so the bar margins are
    // already current here; no explicit call needed.
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

QStringList BarWindow::runtimeWidgetFiles() const
{
    // A customTool with a `source` (vs `exec`) is a runtime QML widget. A widget usually
    // pulls in siblings (a popup .qml, a .js helper), so we hot-reload every .qml/.js in
    // each directory that holds a widget source — not just the entry point named in config.
    const QString configDir = m_config.configFilePath.isEmpty()
        ? QString()
        : QFileInfo(m_config.configFilePath).absolutePath();

    QStringList dirs;
    const auto tools = m_config.customTools;
    for (auto it = tools.constBegin(); it != tools.constEnd(); ++it) {
        const QString source = it.value().toMap().value(QStringLiteral("source")).toString();
        if (source.isEmpty() || source.startsWith(QStringLiteral("qrc:")))
            continue; // empty, or compiled-in and not watchable
        QString path;
        if (source.startsWith(QStringLiteral("file://")))
            path = QUrl(source).toLocalFile();
        else if (source.startsWith(QLatin1Char('/')))
            path = source;
        else if (!configDir.isEmpty())
            path = QDir(configDir).filePath(source);
        const QString dir = path.isEmpty() ? QString() : QFileInfo(path).absolutePath();
        if (!dir.isEmpty() && !dirs.contains(dir))
            dirs.append(dir);
    }

    QStringList files;
    const QStringList filters{QStringLiteral("*.qml"), QStringLiteral("*.js")};
    for (const QString &dir : std::as_const(dirs)) {
        const auto entries = QDir(dir).entryInfoList(filters, QDir::Files);
        for (const QFileInfo &fi : entries) {
            const QString p = fi.absoluteFilePath();
            if (!files.contains(p))
                files.append(p);
        }
    }
    return files;
}

QByteArray BarWindow::widgetContentHash() const
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (const QString &f : std::as_const(m_widgetFiles)) {
        hash.addData(f.toUtf8()); // fold the path in so renames/deletes change the digest
        QFile file(f);
        if (file.open(QIODevice::ReadOnly))
            hash.addData(file.readAll());
    }
    return hash.result();
}

void BarWindow::refreshWidgetWatch()
{
    // Re-enumerate widget files (a save may have added/removed siblings) and reconcile the
    // watcher: editors save atomically (write tmp + rename), which drops a file from the
    // watch list — re-adding here keeps it live. Directories are watched too so a brand-new
    // sibling file triggers a rescan.
    m_widgetFiles = runtimeWidgetFiles();

    QStringList dirs;
    for (const QString &f : std::as_const(m_widgetFiles)) {
        const QString dir = QFileInfo(f).absolutePath();
        if (!dirs.contains(dir))
            dirs.append(dir);
    }

    const QStringList wantFiles = m_widgetFiles;
    const QStringList watchedFiles = m_widgetWatcher->files();
    for (const QString &f : watchedFiles) {
        if (!wantFiles.contains(f))
            m_widgetWatcher->removePath(f);
    }
    for (const QString &f : std::as_const(wantFiles)) {
        if (QFileInfo::exists(f) && !m_widgetWatcher->files().contains(f))
            m_widgetWatcher->addPath(f);
    }
    for (const QString &d : std::as_const(dirs)) {
        if (QFileInfo::exists(d) && !m_widgetWatcher->directories().contains(d))
            m_widgetWatcher->addPath(d);
    }
}

void BarWindow::setupWidgetWatcher()
{
    m_widgetWatcher = new QFileSystemWatcher(this);
    connect(m_widgetWatcher, &QFileSystemWatcher::fileChanged, this, &BarWindow::onWidgetFileChanged);
    connect(m_widgetWatcher, &QFileSystemWatcher::directoryChanged, this, &BarWindow::onWidgetFileChanged);

    refreshWidgetWatch();
    m_widgetHash = widgetContentHash();
}

void BarWindow::onWidgetFileChanged(const QString &)
{
    refreshWidgetWatch();

    // Coalesce the burst of events one save produces by hashing the watched files — a
    // no-op notification (touch, or our own re-add) leaves the digest unchanged.
    const QByteArray digest = widgetContentHash();
    if (digest == m_widgetHash)
        return;
    m_widgetHash = digest;
    reloadWidgets();
}

void BarWindow::reloadWidgets()
{
    // Drop cached QML so the next Loader source-set re-reads the widget from disk, then
    // bump the generation Bar.qml's widget Loaders are bound to (they reload on change).
    engine()->clearComponentCache();
    ++m_widgetReloadGeneration;
    emit widgetReloadGenerationChanged();
}

void BarWindow::onConfigFileChanged(const QString &path)
{
    // Editors atomically replace the file, swapping its inode — re-add so future saves fire.
    if (!m_configWatcher->files().contains(path)) {
        m_configWatcher->addPath(path);
    }
    m_configReloadTimer->start(); // coalesce + wait for the write to settle, then reload
}

void BarWindow::reloadConfigFromDisk()
{
    const QString path = m_config.configFilePath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "qbar: cannot read config file" << path;
        return;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        return; // still mid-save (truncated) — the next fileChanged re-arms the timer
    }
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    if (hash == m_configHash) {
        return; // touch with no content change
    }

    QString jsonError;
    const auto doc = Jsonc::parse(QString::fromUtf8(data), &jsonError);
    QJsonObject root;
    bool foundRoot = false;
    if (doc.isArray()) {
        const QJsonArray bars = doc.array();
        if (m_config.configIndex >= 0 && m_config.configIndex < bars.size() && bars.at(m_config.configIndex).isObject()) {
            root = bars.at(m_config.configIndex).toObject();
            foundRoot = true;
        }
    } else if (doc.isObject()) {
        root = doc.object();
        foundRoot = true;
    }

    if (!foundRoot) {
        // Likely a still-settling write; leave m_configHash untouched so the next event retries.
        qWarning() << "qbar: config.json is broken (invalid JSONC), ignoring reload" << jsonError;
        return;
    }
    m_configHash = hash; // commit only after a clean parse

    const BarConfig fresh = parseBarObject(root);

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

    // Dock options (magnify/indicator/coverflow/heights): forward to the live dock so a
    // config edit re-styles it without a restart.
    if (fresh.dock != m_config.dock) {
        m_config.dock = fresh.dock;
        if (m_dockWindow != nullptr) {
            m_dockWindow->setDockConfig(m_config.dock);
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

QString BarWindow::testWindowCriteria() const
{
    if (m_swayNodeId >= 0) {
        return QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
    }

    return QStringLiteral("[class=\"qbar\"]; [instance=\"qbar\"]; [title=\"QBar\"]");
}

void BarWindow::applyTestWindowRules()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 floating enable; %1 sticky enable; %1 border none").arg(criteria));
        QTimer::singleShot(80, this, SLOT(moveTestWindow()));
        return;
    }

    // i3 and sway (for XWayland windows) both accept class/instance/title
    // criteria. Neither accepts the i3-invalid [pid=…] nor the wayland-only
    // [app_id=…]; on i3 those are unknown tokens that make it reject the whole
    // batched command as a parse error, so the bar never gets floated/unbordered.
    // The native-sway case is handled above via [con_id=…].
    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 floating enable; %1 sticky enable; %1 border none; "
                         "%2 floating enable; %2 sticky enable; %2 border none; "
                         "%3 floating enable; %3 sticky enable; %3 border none")
                         .arg(classCriteria, instanceCriteria, titleCriteria));
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::moveTestWindow()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    qWarning() << "QBar test window target geometry:" << target;
    const int moveY = target.y();
    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5")
                             .arg(criteria)
                             .arg(target.width())
                             .arg(target.height())
                             .arg(target.x())
                             .arg(moveY));
        return;
    }

    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5; "
                         "%6 resize set width %2 px height %3 px; %6 move absolute position %4 %5; "
                         "%7 resize set width %2 px height %3 px; %7 move absolute position %4 %5")
                         .arg(classCriteria)
                         .arg(target.width())
                         .arg(target.height())
                         .arg(target.x())
                         .arg(moveY)
                         .arg(instanceCriteria)
                         .arg(titleCriteria));
}

void BarWindow::handleQbarNodeFound(qint64 nodeId)
{
    m_swayNodeId = nodeId;
    applyTestWindowRules();
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::installTestWindowRule()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    Q_UNUSED(target);
    const QString action = QStringLiteral("floating enable, sticky enable, border none");

    m_wm->runCommand(QStringLiteral("for_window %1 %2; for_window %3 %2; for_window %4 %2")
                         .arg(classCriteria, action, instanceCriteria, titleCriteria));
}

void BarWindow::scheduleTestWindowRules()
{
    if (m_config.waylandLayerShell) {
        return;
    }

    applyTestWindowRules();
    QTimer::singleShot(120, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(150, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(350, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(500, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(900, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1200, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(1500, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1800, this, SLOT(moveTestWindow()));
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
