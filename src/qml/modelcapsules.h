#pragma once

#include <QObject>

#include "../battery/batterymodel.h"
#include "../bluetooth/bluetoothmodel.h"
#include "../brightness/brightnessmodel.h"
#include "../calendar/calendarmodel.h"
#include "../capsule.h"
#include "../cpu/cpumodel.h"
#include "../disk/diskmodel.h"
#include "../mpris/mprismodel.h"
#include "../network/networkmodel.h"
#include "../network/networkprocessmodel.h"
#include "../networkmanager/networkmanagermodel.h"
#include "../powerprofiles/powerprofilesmodel.h"
#include "../privacy/privacymodel.h"
#include "../temperature/temperaturemodel.h"
#include "../upower/upowermodel.h"
#include "../user/usermodel.h"

// Lazy "capsule" registry for the builtin applet backends (CPU, network, bluetooth, …).
//
// Each model is expensive to spin up — a /proc poller on a worker thread, an `ss` subprocess
// every 5s, a pipewire connection, DBus service watchers — so it's wrapped in a Capsule<T>
// that builds it only on the first acquire(). BarWindow acquires a model only when its applet
// is in the config (see BarWindow::buildLayout), so unused backends never run.
//
// It's a PROCESS-WIDE SINGLETON shared by every bar (each BarWindow is its own QQuickView /
// engine), so a model used on more than one bar is created once, not per bar — which also
// removes the previous duplication (e.g. two `ss` subprocesses for top+bottom).
//
// The QML side never sees this: BarWindow hands the acquired model over as a plain context
// property, so applets read `cpuModel.usage` etc. exactly as if it had been created eagerly.

class ModelCapsules final : public QObject {
    Q_OBJECT

public:
    static ModelCapsules *instance();

    // Return the model for `key`, constructing it on first request via its Capsule. Unknown
    // key → nullptr (logged). Keys: cpu, temperature, network, networkProcess, networkManager,
    // brightness, mpris, calendar, battery, disk, bluetooth, powerProfiles, upower, user,
    // privacy.
    QObject *acquire(const QString &key);

private:
    explicit ModelCapsules(QObject *parent = nullptr);

    Capsule<CpuModel> m_cpu{"CpuModel", [this] { return new CpuModel(this); }};
    Capsule<TemperatureModel> m_temperature{"TemperatureModel", [this] { return new TemperatureModel(this); }};
    Capsule<NetworkModel> m_network{"NetworkModel", [this] { return new NetworkModel(this); }};
    Capsule<NetworkProcessModel> m_networkProcess{"NetworkProcessModel", [this] { return new NetworkProcessModel(5000, this); }};
    Capsule<NetworkManagerModel> m_networkManager{"NetworkManagerModel", [this] { return new NetworkManagerModel(this); }};
    Capsule<BrightnessModel> m_brightness{"BrightnessModel", [this] { return new BrightnessModel(this); }};
    Capsule<MprisModel> m_mpris{"MprisModel", [this] { return new MprisModel(this); }};
    Capsule<CalendarModel> m_calendar{"CalendarModel", [this] { return new CalendarModel(this); }};
    Capsule<BatteryModel> m_battery{"BatteryModel", [this] { return new BatteryModel(this); }};
    Capsule<DiskModel> m_disk{"DiskModel", [this] { return new DiskModel(this); }};
    Capsule<BluetoothModel> m_bluetooth{"BluetoothModel", [this] { return new BluetoothModel(this); }};
    Capsule<PowerProfilesModel> m_powerProfiles{"PowerProfilesModel", [this] { return new PowerProfilesModel(this); }};
    Capsule<UpowerModel> m_upower{"UpowerModel", [this] { return new UpowerModel(this); }};
    Capsule<UserModel> m_user{"UserModel", [this] { return new UserModel(this); }};
    Capsule<PrivacyModel> m_privacy{"PrivacyModel", [this] { return new PrivacyModel(this); }};
};
