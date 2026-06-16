#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class QDBusServiceWatcher;

// Bluetooth status via BlueZ (waybar's "bluetooth" module). Enumerates adapters
// and devices with the org.freedesktop.DBus.ObjectManager, then keeps in sync
// from InterfacesAdded/Removed and PropertiesChanged signals — all async, no
// blocking calls on the GUI thread. Reports whether an adapter exists, whether
// it is powered, and the connected devices.
class BluetoothModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed)
    Q_PROPERTY(QStringList connectedDevices READ connectedDevices NOTIFY changed)
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit BluetoothModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool powered() const { return m_powered; }
    int connectedCount() const { return static_cast<int>(m_connectedDevices.size()); }
    QStringList connectedDevices() const { return m_connectedDevices; }
    QString displayText() const;
    QString tooltipText() const;

    // Toggle the first adapter's Powered property.
    Q_INVOKABLE void togglePower();

signals:
    void changed();

private slots:
    void handleInterfacesAdded(const QDBusObjectPath &path, const QVariantMap &interfaces);
    void handleInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void handlePropertiesChanged(const QString &interface,
                                 const QVariantMap &changed,
                                 const QStringList &invalidated);

private:
    void connectToService();
    void disconnectFromService();
    void scheduleRefresh();
    void refresh();

    bool m_present = false;       // org.bluez owns a name
    bool m_available = false;     // at least one adapter
    bool m_powered = false;
    QString m_adapterPath;
    QStringList m_connectedDevices;
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer m_refreshTimer;
};
