#pragma once

#include "config.h"

#include <QObject>
#include <QSharedPointer>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class CssTheme;
class QNetworkAccessManager;

// Owns the bar's CSS pipeline: the CssTheme (and the notifier's optional dedicated one),
// the config→CSS prelude, theme-reference resolution, recursive @import inlining (http
// and file), and the IPC set-css/reset-css preview flow.
class ThemeManager final : public QObject {
    Q_OBJECT

public:
    // `config` is the owning bar's LIVE config, held by reference — and mutated:
    // set-css overwrites config.styleSheet (so later reloads keep the preview) and
    // reset-css restores the remembered configured sheet.
    explicit ThemeManager(BarConfig &config, QObject *parent = nullptr);

    CssTheme *cssTheme() const { return m_cssTheme; }
    CssTheme *notificationCssTheme() const { return m_notificationCssTheme; }

    // (Re)load the bar stylesheet cascade: config-derived prelude, optional base sheet,
    // then the theme itself. The owner connects to cssTheme()'s loadedChanged BEFORE the
    // first load() to see every (re)load.
    void load();

    // Resolve a theme reference from the config: http(s)/file URLs and absolute paths pass
    // through; a bare relative path resolves against the directory of the active config file.
    QString resolveThemeReference(const QString &pathOrUrl) const;

    // Create the notifier's dedicated CssTheme (`notifications.styleSheet`) and load it.
    CssTheme *createNotificationTheme();
    // (Re)load the notifier's dedicated stylesheet. No-op unless createNotificationTheme ran.
    void reloadNotificationTheme();

    // IPC set-css: http(s) URL → fetch + apply; file/path → load from disk.
    bool setStyleSheet(const QString &pathOrUrl);
    // IPC reset-css: revert a set-css preview to the config-specified theme.
    bool resetStyleSheet();
    // A config hot-reload delivered fresh styleSheet/baseStyleSheet values.
    void handleConfigReload(const BarConfig &fresh);

    // The `theme` QVariantMap exposed to QML (colors + contrast-derived accents + fonts).
    static QVariantMap themeMap(const BarConfig &config);

private:
    // Recursively resolve @import in `css` relative to `base` (http or file), fetching/
    // reading each, then invoke `done` with the fully-inlined CSS. Async (http).
    void resolveCssImports(const QString &css, const QUrl &base,
                           QSharedPointer<QStringList> visited,
                           std::function<void(const QString &)> done);

    BarConfig &m_config;
    // The styleSheet the config file specifies — preserved so `reset-css` can revert a
    // `set-css` preview (which overwrites m_config.styleSheet). Tracks config hot-reloads.
    QString m_configuredStyleSheet;
    CssTheme *m_cssTheme = nullptr;
    // Only when `notifications.styleSheet` is configured: the notifier's own theme, so
    // toasts are styled independently of the bar (like the lock's dedicated *-lock.css).
    // Null otherwise — the daemon then shares m_cssTheme.
    CssTheme *m_notificationCssTheme = nullptr;
    QNetworkAccessManager *m_cssNam = nullptr; // lazily created for set-css over http(s)
};
