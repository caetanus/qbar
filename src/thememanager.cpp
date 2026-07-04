#include "thememanager.h"

#include "qmlcss/csstheme.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <chrono>

namespace {

QColor contrastColorFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2933"));
}

QColor contrastGreenFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#86efac")) : QColor(QStringLiteral("#166534"));
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

ThemeManager::ThemeManager(BarConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_configuredStyleSheet(config.styleSheet) // remembered so reset-css can revert a preview
{
    m_cssTheme = new CssTheme(this);
}

QVariantMap ThemeManager::themeMap(const BarConfig &config)
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

void ThemeManager::load()
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
    // instead (see reloadNotificationTheme).
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
    // loadLayered emits loadedChanged — the owner's margin recompute (and any other
    // listener) runs off that signal; nothing to call explicitly here.
}

QString ThemeManager::resolveThemeReference(const QString &pathOrUrl) const
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

CssTheme *ThemeManager::createNotificationTheme()
{
    if (m_notificationCssTheme == nullptr) {
        m_notificationCssTheme = new CssTheme(this);
        reloadNotificationTheme();
    }
    return m_notificationCssTheme;
}

void ThemeManager::reloadNotificationTheme()
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

bool ThemeManager::setStyleSheet(const QString &pathOrUrl)
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
    load();                     // re-layer: keeps baseStyleSheet (if any) under the new theme
    return true;
}

bool ThemeManager::resetStyleSheet()
{
    if (m_cssTheme == nullptr) {
        return false;
    }
    m_config.styleSheet = m_configuredStyleSheet; // revert a set-css preview to the configured theme
    load();
    return true;
}

void ThemeManager::handleConfigReload(const BarConfig &fresh)
{
    if (fresh.styleSheet != m_config.styleSheet || fresh.baseStyleSheet != m_config.baseStyleSheet) {
        m_config.styleSheet = fresh.styleSheet;
        m_configuredStyleSheet = fresh.styleSheet; // keep reset-css pointed at the config's theme
        m_config.baseStyleSheet = fresh.baseStyleSheet;
        load();
    }
}

void ThemeManager::resolveCssImports(const QString &css, const QUrl &base,
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
