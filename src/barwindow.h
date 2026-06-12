#pragma once

#include "config.h"
#include "cpu/cpumodel.h"
#include "temperature/temperaturemodel.h"
#include "brightness/brightnessmodel.h"
#include "caffeine/caffeinemodel.h"
#include "battery/batterymodel.h"
#include "network/networkmodel.h"
#include "networkmanager/networkmanagermodel.h"
#include "sound/soundmodel.h"
#include "ipc/i3ipcclient.h"
#include "tray/statusnotifiermodel.h"

#include <QCalendarWidget>
#include <QFrame>
#include <QWidget>

class QHBoxLayout;
class AppletHost;

class BarWindow final : public QWidget {
    Q_OBJECT

public:
    explicit BarWindow(const BarConfig &config, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void handleAppletActivated(const QString &appletName);
    void applyTestWindowRules();
    void moveTestWindow();
    void handleQbarNodeFound(qint64 nodeId);
    void updateTitleAnchor();

private:
    void configureWindow();
    void buildLayout();
    void positionAtTop();
    QRect targetBarGeometry() const;
    QString testWindowCriteria() const;
    void installTestWindowRule();
    void scheduleTestWindowRules();
    void toggleCalendar(QWidget *anchor);

    BarConfig m_config;
    QHBoxLayout *m_layout = nullptr;
    QFrame *m_calendarPopup = nullptr;
    QCalendarWidget *m_calendar = nullptr;
    AppletHost *m_clockHost = nullptr;
    AppletHost *m_titleHost = nullptr;
    I3IpcClient *m_i3Ipc = nullptr;
    StatusNotifierModel *m_trayModel = nullptr;
    CpuModel *m_cpuModel = nullptr;
    TemperatureModel *m_temperatureModel = nullptr;
    NetworkModel *m_networkModel = nullptr;
    NetworkManagerModel *m_networkManagerModel = nullptr;
    BrightnessModel *m_brightnessModel = nullptr;
    CaffeineModel *m_caffeineModel = nullptr;
    SoundModel *m_soundModel = nullptr;
    BatteryModel *m_batteryModel = nullptr;
    qint64 m_swayNodeId = -1;
    bool m_platformIntegrationApplied = false;
};
