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

class QQuickWidget;
class QBarPopupService;

class BarWindow final : public QWidget {
    Q_OBJECT

public:
    explicit BarWindow(const BarConfig &config, QWidget *parent = nullptr);

public slots:
    void openCalendar(QObject *anchorObject);
    void cycleKeyboardLayout();
    void toggleCaffeine();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void applyTestWindowRules();
    void moveTestWindow();
    void handleQbarNodeFound(qint64 nodeId);

private:
    void configureWindow();
    void buildLayout();
    void positionAtTop();
    QRect targetBarGeometry() const;
    QString testWindowCriteria() const;
    void installTestWindowRule();
    void scheduleTestWindowRules();

    BarConfig m_config;
    QQuickWidget *m_view = nullptr;
    QBarPopupService *m_popupService = nullptr;
    QFrame *m_calendarPopup = nullptr;
    QCalendarWidget *m_calendar = nullptr;
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
