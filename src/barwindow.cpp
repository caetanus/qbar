#include "barwindow.h"

#include "configreloader.h"
#include "thememanager.h"
#include "wm/testwindowrules.h"
#include "qml/baractions.h"
#include "qml/qbarpopupservice.h"
#include "qml/dockwindow.h"
#include "qml/enginebootstrap.h"
#include "qml/qbaripc.h"
#include "qml/widgetreloader.h"
#include "notifications/notificationserver.h"
#include "caffeine/caffeinemodel.h"
#include "platform/capslockmonitor.h"
#include "qmlcss/csstheme.h"

using QmlCss::CssTheme;
#include "dbus/dbusservice.h"
#include "platform/platformbarintegration.h"
#include "wm/wmbackendfactory.h"
#include "ipc/i3ipcclient.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHideEvent>
#include <QPoint>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <unistd.h>

BarWindow::BarWindow(const BarConfig &config, QWindow *parent)
    : QQuickView(parent)
    , m_config(config)
{
    configureWindow();
    m_wm = createWindowManagerBackend(m_config.windowManagerBackend, this);
    m_actions = new BarActions(this, m_wm, m_config, this);
    m_capsLockMonitor = new CapsLockMonitor(this);
    m_themeManager = new ThemeManager(m_config, this);
    // Recompute the bar's CSS edge gap on every theme (re)load. CssTheme's own file watcher
    // reloads the stylesheet WITHOUT going through ThemeManager::load(), so without this the
    // margin would only update on a full restart or theme swap, not on a live CSS edit.
    connect(m_themeManager->cssTheme(), &CssTheme::loadedChanged,
            this, &BarWindow::updateBarMarginsFromCss);
    m_themeManager->load();

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
    return m_actions->calendarAppAvailable();
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

void BarWindow::updateBarMarginsFromCss()
{
    if (m_themeManager == nullptr) {
        return;
    }
    CssTheme *cssTheme = m_themeManager->cssTheme();
    // Bar edge gap from the bar's CSS (#qbar / window#waybar are synonyms here) — margin-top /
    // margin-bottom, or the `margin` shorthand (waybar floating-bar idiom). The layer-shell
    // integration reads these window properties. CSS-ONLY on purpose: the JSON `margin` has
    // never driven the layer gap, so honouring it now would shift every existing config.
    // Horizontal insets stay with Bar.qml (margin-left/right → content inset).
    // Type-qualified match: the waybar-compat rule is `window#waybar { }`, and the
    // vendored engine (correctly) only matches the type selector when told the primitive.
    QVariantMap barRule = cssTheme->resolve(QStringLiteral("waybar"), {}, {}, QStringLiteral("window"));
    const QVariantMap qbarRule = cssTheme->resolveExact(QStringLiteral("qbar"));
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
            shorthandTop = qRound(cssTheme->parseLength(parts.first(), 0));
            shorthandBottom = parts.size() >= 3 ? qRound(cssTheme->parseLength(parts.at(2), 0)) : shorthandTop;
        }
    }
    const auto verticalMargin = [&](const char *key, int shorthandVal) -> int {
        if (barRule.contains(QLatin1String(key))) {
            return qRound(cssTheme->parseLength(barRule.value(QLatin1String(key)).toString(), 0));
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
    return m_themeManager->setStyleSheet(pathOrUrl);
}

bool BarWindow::resetStyleSheet()
{
    return m_themeManager->resetStyleSheet();
}

void BarWindow::applyReloadedConfig(const BarConfig &fresh)
{
    m_themeManager->handleConfigReload(fresh);

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
            if (m_themeManager->notificationCssTheme() != nullptr) {
                m_themeManager->reloadNotificationTheme(); // re-point the dedicated theme at the new file
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

// acquire() is a shared process-wide singleton, so a model used on more than one bar is
// built once. To QML each is an ordinary context property — it can't tell eager from lazy.
void BarWindow::exposeModels()
{
    exposeConfiguredModels(rootContext(), m_config, this);
}

void BarWindow::buildLayout()
{
    const QVariantMap theme = ThemeManager::themeMap(m_config);
    installQbarEngineGlobals(engine());

    m_popupService = new QBarPopupService(engine(), theme, m_wm->workspaceModel(), m_wm,
                                          m_themeManager->cssTheme(), this);
    m_popupService->setOverlayKeyboardFocus(m_config.popupKeyboardFocus);
    m_popupService->setBarWindow(this);
    m_actions->setPopupService(m_popupService);
    connect(m_wm, &WindowManagerBackend::workspaceFocusEvent, m_popupService, &QBarPopupService::closeAll);

    // The macOS-style Dock controller. Cheap to construct and creates no window until
    // the in-bar "Dock" proxy applet first reports a non-empty slot, so bars without a
    // Dock pay nothing. Exposed to QML as `dockController`.
    m_dockWindow = new DockWindow(engine(), theme, m_config.dock, m_wm->windowModel(), m_wm,
                                  m_themeManager->cssTheme(), this);
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
        QObject *notificationTheme = m_themeManager->cssTheme();
        if (!m_config.notifications.value(QStringLiteral("styleSheet")).toString().isEmpty()) {
            notificationTheme = m_themeManager->createNotificationTheme();
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
    rootContext()->setContextProperty(QStringLiteral("cssTheme"), m_themeManager->cssTheme());
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
    m_actions->openCalendar(anchorObject);
}

void BarWindow::openEvolutionCalendar()
{
    m_actions->openEvolutionCalendar();
}

void BarWindow::cycleKeyboardLayout()
{
    m_actions->cycleKeyboardLayout();
}

void BarWindow::toggleCaffeine()
{
    m_actions->toggleCaffeine();
}
