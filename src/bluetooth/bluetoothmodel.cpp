#include "bluetoothmodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

namespace {
constexpr auto kService = "org.bluez";
constexpr auto kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr auto kAdapterInterface = "org.bluez.Adapter1";
constexpr auto kDeviceInterface = "org.bluez.Device1";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";
} // namespace

// GetManagedObjects returns a{oa{sa{sv}}}. Register the typed containers so
// QDBusPendingReply demarshals them with QtDBus' streaming operators (manual
// QDBusArgument iteration on the read-only reply triggers "write from a
// read-only object" and yields nothing).
using BluezInterfaceMap = QMap<QString, QVariantMap>;
using BluezManagedObjects = QMap<QDBusObjectPath, BluezInterfaceMap>;
Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezManagedObjects)

BluetoothModel::BluetoothModel(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<BluezInterfaceMap>();
    qDBusRegisterMetaType<BluezManagedObjects>();

    // Coalesce the bursts of signals BlueZ emits when a device connects.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150);
    connect(&m_refreshTimer, &QTimer::timeout, this, &BluetoothModel::refresh);

    m_watcher = new QDBusServiceWatcher(QString::fromLatin1(kService), QDBusConnection::systemBus(),
                                        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) {
                    disconnectFromService();
                } else {
                    connectToService();
                }
            });

    if (auto *bus = QDBusConnection::systemBus().interface();
        bus != nullptr && bus->isServiceRegistered(QString::fromLatin1(kService))) {
        connectToService();
    }
}

void BluetoothModel::connectToService()
{
    if (m_present) {
        return;
    }
    m_present = true;

    QDBusConnection systemBus = QDBusConnection::systemBus();
    // Track the object graph and any property change under org.bluez (empty path
    // matches every object), then debounce into a single GetManagedObjects.
    systemBus.connect(QString::fromLatin1(kService), QStringLiteral("/"),
                      QString::fromLatin1(kObjectManagerInterface), QStringLiteral("InterfacesAdded"),
                      this, SLOT(handleInterfacesAdded(QDBusObjectPath, QVariantMap)));
    systemBus.connect(QString::fromLatin1(kService), QStringLiteral("/"),
                      QString::fromLatin1(kObjectManagerInterface), QStringLiteral("InterfacesRemoved"),
                      this, SLOT(handleInterfacesRemoved(QDBusObjectPath, QStringList)));
    systemBus.connect(QString::fromLatin1(kService), QString(),
                      QString::fromLatin1(kPropsInterface), QStringLiteral("PropertiesChanged"),
                      this, SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));
    refresh();
}

void BluetoothModel::disconnectFromService()
{
    if (!m_present) {
        return;
    }
    QDBusConnection systemBus = QDBusConnection::systemBus();
    systemBus.disconnect(QString::fromLatin1(kService), QStringLiteral("/"),
                         QString::fromLatin1(kObjectManagerInterface), QStringLiteral("InterfacesAdded"),
                         this, SLOT(handleInterfacesAdded(QDBusObjectPath, QVariantMap)));
    systemBus.disconnect(QString::fromLatin1(kService), QStringLiteral("/"),
                         QString::fromLatin1(kObjectManagerInterface), QStringLiteral("InterfacesRemoved"),
                         this, SLOT(handleInterfacesRemoved(QDBusObjectPath, QStringList)));
    systemBus.disconnect(QString::fromLatin1(kService), QString(),
                         QString::fromLatin1(kPropsInterface), QStringLiteral("PropertiesChanged"),
                         this, SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));
    m_present = false;
    m_available = false;
    m_powered = false;
    m_adapterPath.clear();
    m_connectedDevices.clear();
    emit changed();
}

void BluetoothModel::handleInterfacesAdded(const QDBusObjectPath &, const QVariantMap &)
{
    scheduleRefresh();
}

void BluetoothModel::handleInterfacesRemoved(const QDBusObjectPath &, const QStringList &)
{
    scheduleRefresh();
}

void BluetoothModel::handlePropertiesChanged(const QString &interface, const QVariantMap &, const QStringList &)
{
    if (interface == QLatin1String(kAdapterInterface) || interface == QLatin1String(kDeviceInterface)) {
        scheduleRefresh();
    }
}

void BluetoothModel::scheduleRefresh()
{
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void BluetoothModel::refresh()
{
    if (!m_present) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QStringLiteral("/"),
                                               QString::fromLatin1(kObjectManagerInterface),
                                               QStringLiteral("GetManagedObjects"));
    auto pending = QDBusConnection::systemBus().asyncCall(call);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<BluezManagedObjects> reply = *w;
        if (reply.isError()) {
            return;
        }
        const BluezManagedObjects objects = reply.value();

        bool available = false;
        bool powered = false;
        QString adapterPath;
        QStringList connected;

        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
            const BluezInterfaceMap &interfaces = it.value();
            if (interfaces.contains(QString::fromLatin1(kAdapterInterface))) {
                available = true;
                if (adapterPath.isEmpty()) {
                    adapterPath = it.key().path();
                    powered = interfaces.value(QString::fromLatin1(kAdapterInterface))
                                  .value(QStringLiteral("Powered")).toBool();
                }
            }
            if (interfaces.contains(QString::fromLatin1(kDeviceInterface))) {
                const QVariantMap props = interfaces.value(QString::fromLatin1(kDeviceInterface));
                if (props.value(QStringLiteral("Connected")).toBool()) {
                    QString name = props.value(QStringLiteral("Alias")).toString();
                    if (name.isEmpty()) {
                        name = props.value(QStringLiteral("Name")).toString();
                    }
                    if (name.isEmpty()) {
                        name = props.value(QStringLiteral("Address")).toString();
                    }
                    connected.append(name);
                }
            }
        }
        connected.sort(Qt::CaseInsensitive);

        if (available == m_available && powered == m_powered && adapterPath == m_adapterPath
            && connected == m_connectedDevices) {
            return;
        }
        m_available = available;
        m_powered = powered;
        m_adapterPath = adapterPath;
        m_connectedDevices = connected;
        emit changed();
    });
}

void BluetoothModel::togglePower()
{
    if (m_adapterPath.isEmpty()) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(QString::fromLatin1(kService), m_adapterPath,
                                               QString::fromLatin1(kPropsInterface), QStringLiteral("Set"));
    call << QString::fromLatin1(kAdapterInterface) << QStringLiteral("Powered")
         << QVariant::fromValue(QDBusVariant(!m_powered));
    QDBusConnection::systemBus().asyncCall(call);
}

QString BluetoothModel::displayText() const
{
    if (!m_powered) {
        return QStringLiteral("off");
    }
    if (!m_connectedDevices.isEmpty()) {
        return QString::number(m_connectedDevices.size());
    }
    return QStringLiteral("on");
}

QString BluetoothModel::tooltipText() const
{
    if (!m_available) {
        return QStringLiteral("No Bluetooth adapter");
    }
    if (!m_powered) {
        return QStringLiteral("Bluetooth off");
    }
    if (m_connectedDevices.isEmpty()) {
        return QStringLiteral("Bluetooth on — no devices connected");
    }
    return QStringLiteral("Connected: %1").arg(m_connectedDevices.join(QStringLiteral(", ")));
}
