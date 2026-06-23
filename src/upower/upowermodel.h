#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QDBusServiceWatcher;

// Peripheral battery levels via UPower (waybar's "upower" module). Enumerates UPower
// devices and reports the NON-power-supply ones (mouse, keyboard, headset, controller…) —
// the laptop's own battery/AC (PowerSupply=true) is left to the Battery applet. All async:
// EnumerateDevices + per-device GetAll, kept in sync from DeviceAdded/Removed and each
// device's PropertiesChanged, with a debounce. Exposes `devices` as a QVariantList of maps
// (percentage/state/isCharging/iconName/model/type) the QML iterates directly.
class UpowerModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit UpowerModel(QObject *parent = nullptr);

    bool available() const { return !m_devices.isEmpty(); }
    QVariantList devices() const { return m_devices; }
    QString tooltipText() const;

signals:
    void changed();

private slots:
    void handleDeviceListChanged();  // DeviceAdded/DeviceRemoved → debounced refresh
    void handleDevicePropertiesChanged(const QString &interface,
                                       const QVariantMap &changed,
                                       const QStringList &invalidated);

private:
    void connectToService();
    void disconnectFromService();
    void scheduleRefresh();
    void refresh();
    void watchDevice(const QString &path);
    void rebuildDevices();         // filter m_deviceData → m_devices (peripherals only), emit

    bool m_present = false;        // org.freedesktop.UPower owns a name
    QString m_service;
    QVariantList m_devices;        // built, filtered, QML-facing list
    QHash<QString, QVariantMap> m_deviceData;  // path → raw device props
    QSet<QString> m_watchedPaths;  // device paths we've subscribed to PropertiesChanged
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer m_refreshTimer;
};
