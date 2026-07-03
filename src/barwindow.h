#pragma once

#include "config.h"
#include "platform/capslockmonitor.h"
#include "css/csstheme.h"
#include "wm/windowmanagerbackend.h"

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QQuickView>
#include <QSharedPointer>
#include <QStringList>

#include <functional>

class QBarPopupService;
class DockWindow;
class QNetworkAccessManager;
class QTimer;
class QUrl;

class BarWindow final : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(bool calendarAppAvailable READ calendarAppAvailable CONSTANT)
    // Bumped whenever a runtime custom-widget QML file changes on disk; Bar.qml binds
    // its widget Loaders to this and reloads them from disk on each bump (hot-reload).
    Q_PROPERTY(int widgetReloadGeneration READ widgetReloadGeneration NOTIFY widgetReloadGenerationChanged)

public:
    explicit BarWindow(const BarConfig &config, QWindow *parent = nullptr);

public slots:
    void openCalendar(QObject *anchorObject);
    void openEvolutionCalendar();
    void cycleKeyboardLayout();
    void toggleCaffeine();
    // Swap the CSS theme at runtime (IPC `set-css` — handy for previewing a theme). Accepts an
    // http(s) URL, a file:// URL, or a path. (qbar-ipc resolves a bare/relative name to an
    // absolute path against ITS OWN working directory before sending, since the daemon's cwd
    // differs.) Returns false if empty or the file is missing.
    bool setStyleSheet(const QString &path);
    // Revert a `set-css` preview back to the theme the config file specifies (IPC `reset-css`).
    bool resetStyleSheet();

public:
    bool calendarAppAvailable() const;
    int widgetReloadGeneration() const { return m_widgetReloadGeneration; }

signals:
    void widgetReloadGenerationChanged();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void applyTestWindowRules();
    void moveTestWindow();
    void handleQbarNodeFound(qint64 nodeId);
    void onConfigFileChanged(const QString &path);
    void reloadConfigFromDisk();
    void onWidgetFileChanged(const QString &path);

private:
    void configureWindow();
    void exposeModels();
    void loadCssTheme();
    // Resolve a theme reference from the config: http(s)/file URLs and absolute paths pass
    // through; a bare relative path resolves against the directory of the active config file.
    QString resolveThemeReference(const QString &pathOrUrl) const;
    // (Re)load the notifier's dedicated stylesheet (`notifications.styleSheet`) into
    // m_notificationCssTheme. No-op unless the daemon was created with its own theme.
    void loadNotificationCssTheme();
    // Re-derive the bar's edge gap (CSS margin-top/bottom) into the qbarBarMargin* window
    // properties the layer-shell plugin reads. Connected to CssTheme::loadedChanged so it runs
    // on every (re)load — including the CSS hot-reload path that bypasses loadCssTheme().
    void updateBarMarginsFromCss();
    // Recursively resolve @import in `css` relative to `base` (http or file), fetching/
    // reading each, then invoke `done` with the fully-inlined CSS. Async (http).
    void resolveCssImports(const QString &css, const QUrl &base,
                           QSharedPointer<QStringList> visited,
                           std::function<void(const QString &)> done);
    void buildLayout();
    void positionAtTop();
    QRect targetBarGeometry() const;
    QString testWindowCriteria() const;
    void installTestWindowRule();
    void scheduleTestWindowRules();
    void setupWidgetWatcher();
    void refreshWidgetWatch();
    void reloadWidgets();
    QStringList runtimeWidgetFiles() const;
    QByteArray widgetContentHash() const;

    BarConfig m_config;
    // The styleSheet the config file specifies — preserved so `reset-css` can revert a
    // `set-css` preview (which overwrites m_config.styleSheet). Tracks config hot-reloads.
    QString m_configuredStyleSheet;
    QBarPopupService *m_popupService = nullptr;
    DockWindow *m_dockWindow = nullptr;
    QString m_calendarPopupId;
    QString m_evolutionCalendarExecutable;
    WindowManagerBackend *m_wm = nullptr;
    CapsLockMonitor *m_capsLockMonitor = nullptr;
    CssTheme *m_cssTheme = nullptr;
    // Only when `notifications.styleSheet` is configured: the notifier's own theme, so
    // toasts are styled independently of the bar (like the lock's dedicated *-lock.css).
    // Null otherwise — the daemon then shares m_cssTheme.
    CssTheme *m_notificationCssTheme = nullptr;
    QNetworkAccessManager *m_cssNam = nullptr; // lazily created for set-css over http(s)
    QFileSystemWatcher *m_configWatcher = nullptr;
    QTimer *m_configReloadTimer = nullptr; // debounces save-in-progress watcher events
    QByteArray m_configHash;
    QFileSystemWatcher *m_widgetWatcher = nullptr;
    QStringList m_widgetFiles;
    QByteArray m_widgetHash;
    int m_widgetReloadGeneration = 0;
    qint64 m_swayNodeId = -1;
    bool m_platformIntegrationApplied = false;
};
