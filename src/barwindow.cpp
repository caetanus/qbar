#include "barwindow.h"

#include "applethost.h"
#include "cpu/cpumodel.h"
#include "temperature/temperaturemodel.h"
#include "brightness/brightnessmodel.h"
#include "caffeine/caffeinemodel.h"
#include "battery/batterymodel.h"
#include "network/networkmodel.h"
#include "networkmanager/networkmanagermodel.h"
#include "sound/soundmodel.h"
#include "platform/platformbarintegration.h"

#include <QApplication>
#include <QByteArray>
#include <algorithm>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QCloseEvent>
#include <QDebug>
#include <QHideEvent>
#include <QPoint>
#include <QQuickItem>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <unistd.h>

namespace {

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

} // namespace

BarWindow::BarWindow(const BarConfig &config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    configureWindow();
    m_i3Ipc = new I3IpcClient(this);
    m_trayModel = new StatusNotifierModel(this);
    m_cpuModel = new CpuModel(this);
    m_temperatureModel = new TemperatureModel(this);
    m_networkModel = new NetworkModel(this);
    m_networkManagerModel = new NetworkManagerModel(this);
    m_brightnessModel = new BrightnessModel(this);
    m_caffeineModel = new CaffeineModel(this);
    m_soundModel = new SoundModel(this);
    m_batteryModel = new BatteryModel(this);
    connect(m_i3Ipc, SIGNAL(qbarNodeFound(qint64)), this, SLOT(handleQbarNodeFound(qint64)));
    buildLayout();
    positionAtTop();
    m_i3Ipc->start();
    m_trayModel->start();
}

void BarWindow::closeEvent(QCloseEvent *event)
{
    qWarning() << "QBar closeEvent";
    QWidget::closeEvent(event);
    QApplication::quit();
}

void BarWindow::hideEvent(QHideEvent *event)
{
    qWarning() << "QBar hideEvent";
    QWidget::hideEvent(event);
}

void BarWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, SLOT(updateTitleAnchor()));
}

void BarWindow::showEvent(QShowEvent *event)
{
    qWarning() << "QBar showEvent";
    QWidget::showEvent(event);

    if (!m_platformIntegrationApplied) {
        applyPlatformBarIntegration(this, m_config);
        scheduleTestWindowRules();
        m_platformIntegrationApplied = true;
    }
}

void BarWindow::configureWindow()
{
    setWindowTitle(QStringLiteral("QBar"));
    setProperty("appId", QStringLiteral("qbar"));
    setProperty("desktopFileName", QStringLiteral("qbar"));
    const int windowHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
    setMinimumHeight(windowHeight);
    setMaximumHeight(windowHeight);
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (!m_config.waylandLayerShell) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setStyleSheet(QStringLiteral(
                      "QWidget#BarSurface {"
                      "background: %1;"
                      "border-radius: 0px;"
                      "color: %2;"
                      "font-family: \"%3\";"
                      "font-size: %4pt;"
                      "}"
                      "QFrame#CalendarPopup {"
                      "background: %1;"
                      "border: 1px solid rgba(255, 255, 255, 48);"
                      "border-radius: 2px;"
                      "}"
                      "QCalendarWidget { color: %2; }")
                      .arg(m_config.background.name(QColor::HexArgb),
                           m_config.foreground.name(QColor::HexArgb),
                           m_config.fontFamily)
                      .arg(m_config.fontSize));
}

void BarWindow::buildLayout()
{
    auto *surface = new QWidget(this);
    surface->setObjectName(QStringLiteral("BarSurface"));
    surface->setFixedHeight(m_config.height);

    m_layout = new QHBoxLayout(surface);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(m_config.spacing);

    for (const auto &name : m_config.applets) {
        if (name == QStringLiteral("Temperature") && (m_temperatureModel == nullptr || !m_temperatureModel->available())) {
            qWarning() << "[bar] skipping applet" << name << "because no temperature sensors were found";
            continue;
        }
        qWarning() << "[bar] creating applet" << name;
        auto *host = new AppletHost(name,
                                     m_config,
                                     m_i3Ipc->workspaceModel(),
                                     m_cpuModel,
                                     m_temperatureModel,
                                     m_networkModel,
                                     m_networkManagerModel,
                                     m_brightnessModel,
                                     m_caffeineModel,
                                     m_soundModel,
                                     m_i3Ipc,
                                     m_trayModel,
                                     m_batteryModel,
                                     surface);
        connect(host, SIGNAL(activated(QString)), this, SLOT(handleAppletActivated(QString)));
        connect(host, SIGNAL(workspaceActivated(QString)), m_i3Ipc, SLOT(activateWorkspace(QString)));
        connect(host, SIGNAL(workspaceScrolled(int)), m_i3Ipc, SLOT(activateRelativeWorkspace(int)));
        connect(host, SIGNAL(preferredWidthChanged()), this, SLOT(updateTitleAnchor()));
        m_layout->addWidget(host);

        if (name == QStringLiteral("Title")) {
            m_titleHost = host;
            m_layout->setStretch(m_layout->count() - 1, 1);
        } else if (name == QStringLiteral("Clock")) {
            m_clockHost = host;
        }
    }

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(m_config.margin, 0, m_config.margin, 0);
    outer->setSpacing(0);
    outer->addWidget(surface);

    m_calendarPopup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_calendarPopup->setObjectName(QStringLiteral("CalendarPopup"));
    m_calendarPopup->setWindowTitle(QStringLiteral("QBar Calendar Popup"));
    m_calendarPopup->setAttribute(Qt::WA_TranslucentBackground, true);
    m_calendarPopup->setFocusPolicy(Qt::StrongFocus);

    auto *popupLayout = new QVBoxLayout(m_calendarPopup);
    popupLayout->setContentsMargins(10, 10, 10, 10);
    m_calendar = new QCalendarWidget(m_calendarPopup);
    popupLayout->addWidget(m_calendar);
    QTimer::singleShot(0, this, SLOT(updateTitleAnchor()));
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
    const int targetHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
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

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    return QStringLiteral("%1; [app_id=\"qbar\"]; [title=\"QBar\"]").arg(pidCriteria);
}

void BarWindow::applyTestWindowRules()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_i3Ipc->runCommand(QStringLiteral("%1 floating enable; %1 sticky enable; %1 border none").arg(criteria));
        QTimer::singleShot(80, this, SLOT(moveTestWindow()));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_i3Ipc->runCommand(QStringLiteral(
                            "%1 floating enable; %1 sticky enable; %1 border none; "
                            "%2 floating enable; %2 sticky enable; %2 border none; "
                            "%3 floating enable; %3 sticky enable; %3 border none")
                            .arg(pidCriteria, appCriteria, titleCriteria));
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::moveTestWindow()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    qWarning() << "QBar test window target geometry:" << target;
    const int moveY = target.y();
    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_i3Ipc->runCommand(QStringLiteral("%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5")
                                .arg(criteria)
                                .arg(target.width())
                                .arg(target.height())
                                .arg(target.x())
                                .arg(moveY));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_i3Ipc->runCommand(QStringLiteral(
                            "%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5; "
                            "%6 resize set width %2 px height %3 px; %6 move absolute position %4 %5; "
                            "%7 resize set width %2 px height %3 px; %7 move absolute position %4 %5")
                            .arg(pidCriteria)
                            .arg(target.width())
                            .arg(target.height())
                            .arg(target.x())
                            .arg(moveY)
                            .arg(appCriteria)
                            .arg(titleCriteria));
}

void BarWindow::handleQbarNodeFound(qint64 nodeId)
{
    m_swayNodeId = nodeId;
    applyTestWindowRules();
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::updateTitleAnchor()
{
    if (m_titleHost == nullptr || m_titleHost->rootObject() == nullptr) {
        return;
    }

    const QPoint barCenter = mapToGlobal(QPoint(width() / 2, height() / 2));
    const QPoint titleCenter = m_titleHost->mapFromGlobal(barCenter);
    m_titleHost->rootObject()->setProperty("barCenterX", titleCenter.x());
}

void BarWindow::installTestWindowRule()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    Q_UNUSED(target);
    const QString action = QStringLiteral("floating enable, sticky enable, border none");

    m_i3Ipc->runCommand(QStringLiteral("for_window %1 %2; for_window %3 %2; for_window %4 %2")
                            .arg(pidCriteria, action, appCriteria, titleCriteria));
}

void BarWindow::scheduleTestWindowRules()
{
    if (m_config.waylandLayerShell) {
        return;
    }

    applyTestWindowRules();
    QTimer::singleShot(120, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(150, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(350, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(500, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(900, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1200, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(1500, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1800, this, SLOT(moveTestWindow()));
}

void BarWindow::handleAppletActivated(const QString &appletName)
{
    if (appletName == QStringLiteral("Clock")) {
        toggleCalendar(qobject_cast<QWidget *>(sender()));
    } else if (appletName == QStringLiteral("Caffeine")) {
        m_caffeineModel->toggle();
    } else if (appletName == QStringLiteral("XInput")) {
        m_i3Ipc->cycleKeyboardLayout();
    }
}

void BarWindow::toggleCalendar(QWidget *anchor)
{
    if (m_calendarPopup->isVisible()) {
        m_calendarPopup->hide();
        return;
    }

    if (anchor == nullptr) {
        anchor = m_clockHost != nullptr ? static_cast<QWidget *>(m_clockHost) : static_cast<QWidget *>(this);
    }
    if (m_clockHost != nullptr) {
        anchor = m_clockHost;
    }

    const int popupWidth = 340;
    const int popupHeight = 280;
    const QSize popupSize(popupWidth, popupHeight);
    const int gap = 0;
    const QPoint anchorTopLeft = anchor->mapToGlobal(QPoint(0, 0));
    const QPoint anchorBottomRight = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height()));
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

    const QPoint barTopLeft = mapToGlobal(QPoint(0, 0));
    const QPoint anchorRelativeTopLeft = anchorTopLeft - barTopLeft;
    qputenv("QBAR_LAYER_POPUP_ANCHOR_X", QByteArray::number(anchorRelativeTopLeft.x()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_Y", QByteArray::number(anchorRelativeTopLeft.y()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_WIDTH", QByteArray::number(anchor->width()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_HEIGHT", QByteArray::number(anchor->height()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_TITLE", QByteArray("QBar Calendar Popup"));

    m_calendarPopup->resize(popupSize);
    m_calendarPopup->move(x, y);
    m_calendarPopup->show();
    m_calendarPopup->raise();
    m_calendarPopup->setFocus(Qt::PopupFocusReason);
}
