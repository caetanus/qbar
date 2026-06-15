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
#include "calendar/calendarmodel.h"
#include "css/csstheme.h"
#include "tray/statusnotifiermodel.h"
#include "wm/windowmanagerbackend.h"

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QQuickView>
#include <QStringList>

class QBarPopupService;

class BarWindow final : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(bool calendarAppAvailable READ calendarAppAvailable CONSTANT)

public:
    explicit BarWindow(const BarConfig &config, QWindow *parent = nullptr);

public slots:
    void openCalendar(QObject *anchorObject);
    void openEvolutionCalendar();
    void cycleKeyboardLayout();
    void toggleCaffeine();

public:
    bool calendarAppAvailable() const;

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

private:
    void configureWindow();
    void loadCssTheme();
    void buildLayout();
    void positionAtTop();
    QRect targetBarGeometry() const;
    QString testWindowCriteria() const;
    void installTestWindowRule();
    void scheduleTestWindowRules();

    BarConfig m_config;
    QBarPopupService *m_popupService = nullptr;
    QString m_calendarPopupId;
    QString m_evolutionCalendarExecutable;
    WindowManagerBackend *m_wm = nullptr;
    StatusNotifierModel *m_trayModel = nullptr;
    CpuModel *m_cpuModel = nullptr;
    TemperatureModel *m_temperatureModel = nullptr;
    NetworkModel *m_networkModel = nullptr;
    NetworkManagerModel *m_networkManagerModel = nullptr;
    BrightnessModel *m_brightnessModel = nullptr;
    CaffeineModel *m_caffeineModel = nullptr;
    SoundModel *m_soundModel = nullptr;
    CalendarModel *m_calendarModel = nullptr;
    BatteryModel *m_batteryModel = nullptr;
    CssTheme *m_cssTheme = nullptr;
    QFileSystemWatcher *m_configWatcher = nullptr;
    QByteArray m_configHash;
    qint64 m_swayNodeId = -1;
    bool m_platformIntegrationApplied = false;
};
