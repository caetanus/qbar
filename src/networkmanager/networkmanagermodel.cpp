#include "networkmanagermodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QByteArray>
#include <QDebug>
#include <QVariantList>

namespace {

constexpr auto nmService = "org.freedesktop.NetworkManager";
constexpr auto nmPath = "/org/freedesktop/NetworkManager";
constexpr auto nmRootInterface = "org.freedesktop.NetworkManager";
constexpr auto nmPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto nmActiveConnectionInterface = "org.freedesktop.NetworkManager.Connection.Active";
constexpr auto nmDeviceInterface = "org.freedesktop.NetworkManager.Device";
constexpr auto nmWirelessInterface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto nmAccessPointInterface = "org.freedesktop.NetworkManager.AccessPoint";

constexpr int nmStateConnectedLocal = 50;

QList<QDBusObjectPath> objectPathListFromVariant(const QVariant &value)
{
    QList<QDBusObjectPath> result;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        argument.beginArray();
        while (!argument.atEnd()) {
            QDBusObjectPath path;
            argument >> path;
            result.append(path);
        }
        argument.endArray();
        return result;
    }

    const QVariantList list = value.toList();
    result.reserve(list.size());
    for (const QVariant &item : list) {
        if (item.canConvert<QDBusObjectPath>()) {
            result.append(item.value<QDBusObjectPath>());
        } else {
            const QString path = item.toString();
            if (!path.isEmpty()) {
                result.append(QDBusObjectPath(path));
            }
        }
    }
    return result;
}

QString objectPathValue(const QVariant &value)
{
    if (value.canConvert<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }
    return value.toString();
}

} // namespace

NetworkManagerModel::NetworkManagerModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    QTimer::singleShot(1000, this, &NetworkManagerModel::refresh);
    m_timer.setInterval(2000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &NetworkManagerModel::refresh);
    m_timer.start();
}

QString NetworkManagerModel::mode() const
{
    return modeName(m_state);
}

QString NetworkManagerModel::iconName() const
{
    if (m_state == State::Wireless) {
        return iconNameForStrength(m_strength);
    }

    if (m_state == State::Wired) {
        return QStringLiteral("network-wired-symbolic");
    }

    return QStringLiteral("network-wired-disconnected-symbolic");
}

QString NetworkManagerModel::label() const
{
    if (m_state == State::Wireless) {
        return QStringLiteral("%1%").arg(qBound(0, m_strength, 100));
    }

    return {};
}

QString NetworkManagerModel::interfaceName() const
{
    return m_interfaceName;
}

QString NetworkManagerModel::ssid() const
{
    return m_ssid;
}

int NetworkManagerModel::channel() const
{
    return m_channel;
}

QString NetworkManagerModel::linkSpeedText() const
{
    return m_linkSpeedText;
}

QString NetworkManagerModel::tooltipText() const
{
    if (m_state == State::Wireless) {
        const QString strengthText = QStringLiteral("%1%").arg(qBound(0, m_strength, 100));
        const QString bandText = m_channel > 14 ? QStringLiteral("5G") : QStringLiteral("2G");
        const QString channelText = m_channel > 0 ? QStringLiteral("chan %1").arg(m_channel) : QString();
        const QString linkText = !m_linkSpeedText.isEmpty() ? QStringLiteral("link %1").arg(m_linkSpeedText) : QString();
        const QStringList parts = {
            m_ssid,
            m_interfaceName,
            bandText,
            channelText,
            linkText,
            strengthText,
        };
        QStringList filtered;
        filtered.reserve(parts.size());
        for (const QString &part : parts) {
            if (!part.isEmpty()) {
                filtered.append(part);
            }
        }
        return filtered.join(QStringLiteral(" | "));
    }

    if (m_state == State::Wired) {
        const QStringList parts = {
            m_interfaceName.isEmpty() ? QStringLiteral("ethernet") : m_interfaceName,
            !m_linkSpeedText.isEmpty() ? QStringLiteral("link %1").arg(m_linkSpeedText) : QString(),
        };
        QStringList filtered;
        filtered.reserve(parts.size());
        for (const QString &part : parts) {
            if (!part.isEmpty()) {
                filtered.append(part);
            }
        }
        return filtered.join(QStringLiteral(" | "));
    }

    return QStringLiteral("network disconnected");
}

int NetworkManagerModel::strength() const
{
    return m_strength;
}

bool NetworkManagerModel::available() const
{
    return true;
}

QVariant NetworkManagerModel::readProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    QDBusInterface iface(service, path, nmPropertiesInterface, QDBusConnection::systemBus());
    if (!iface.isValid()) {
        return {};
    }

    const QDBusReply<QVariant> reply = iface.call(QStringLiteral("Get"), interface, name);
    if (!reply.isValid()) {
        return {};
    }

    QVariant value = reply.value();
    if (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

QString NetworkManagerModel::readStringProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    return readProperty(service, path, interface, name).toString();
}

int NetworkManagerModel::readIntProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    const QVariant value = readProperty(service, path, interface, name);
    return value.isValid() ? value.toInt() : 0;
}

QString NetworkManagerModel::readPrimaryConnectionType() const
{
    return readStringProperty(nmService, nmPath, nmRootInterface, QStringLiteral("PrimaryConnectionType"));
}

QString NetworkManagerModel::readPrimaryConnectionPath() const
{
    return objectPathValue(readProperty(nmService, nmPath, nmRootInterface, QStringLiteral("PrimaryConnection")));
}

int NetworkManagerModel::readWirelessStrength(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(nmService, devicePath, nmWirelessInterface, QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return 0;
    }

    return qBound(0, readIntProperty(nmService, accessPointPath, nmAccessPointInterface, QStringLiteral("Strength")), 100);
}

int NetworkManagerModel::readWirelessFrequency(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(nmService, devicePath, nmWirelessInterface, QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return 0;
    }

    return readIntProperty(nmService, accessPointPath, nmAccessPointInterface, QStringLiteral("Frequency"));
}

int NetworkManagerModel::readWirelessBitrate(const QString &devicePath) const
{
    return readIntProperty(nmService, devicePath, nmWirelessInterface, QStringLiteral("Bitrate"));
}

int NetworkManagerModel::readEthernetSpeed(const QString &devicePath) const
{
    return readIntProperty(nmService, devicePath, nmDeviceInterface, QStringLiteral("Speed"));
}

QString NetworkManagerModel::readWirelessSsid(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(nmService, devicePath, nmWirelessInterface, QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return {};
    }

    const QVariant value = readProperty(nmService, accessPointPath, nmAccessPointInterface, QStringLiteral("Ssid"));
    if (!value.isValid()) {
        return {};
    }

    QByteArray bytes = value.toByteArray();
    if (bytes.isEmpty() && value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        argument.beginArray();
        while (!argument.atEnd()) {
            uchar byte = 0;
            argument >> byte;
            bytes.append(static_cast<char>(byte));
        }
        argument.endArray();
    }
    if (bytes.isEmpty() && value.canConvert<QString>()) {
        return value.toString();
    }

    const QString ssid = QString::fromUtf8(bytes.constData(), bytes.size()).trimmed();
    return ssid;
}

int NetworkManagerModel::channelFromFrequency(int frequency)
{
    if (frequency <= 0) {
        return 0;
    }
    if (frequency == 2484) {
        return 14;
    }
    if (frequency >= 2412 && frequency <= 2472) {
        return (frequency - 2407) / 5;
    }
    if (frequency >= 5000 && frequency <= 5900) {
        return (frequency - 5000) / 5;
    }
    if (frequency >= 5955 && frequency <= 7115) {
        return (frequency - 5950) / 5;
    }
    return 0;
}

QString NetworkManagerModel::linkSpeedString(double mbps)
{
    if (mbps <= 0.0) {
        return {};
    }

    return QStringLiteral("%1 Mbps").arg(QString::number(mbps, 'f', mbps < 10.0 ? 1 : 0).replace(QStringLiteral(".0"), QString()));
}

QString NetworkManagerModel::iconNameForStrength(int strength)
{
    if (strength <= 0) {
        return QStringLiteral("network-wireless-signal-none-symbolic");
    }
    if (strength < 25) {
        return QStringLiteral("network-wireless-signal-weak-symbolic");
    }
    if (strength < 50) {
        return QStringLiteral("network-wireless-signal-ok-symbolic");
    }
    if (strength < 75) {
        return QStringLiteral("network-wireless-signal-good-symbolic");
    }
    return QStringLiteral("network-wireless-signal-excellent-symbolic");
}

QString NetworkManagerModel::modeName(State state)
{
    switch (state) {
    case State::Wireless:
        return QStringLiteral("wireless");
    case State::Wired:
        return QStringLiteral("wired");
    case State::Disconnected:
    default:
        return QStringLiteral("disconnected");
    }
}

void NetworkManagerModel::setState(State state,
                                   int strength,
                                   const QString &interfaceName,
                                   const QString &ssid,
                                   int channel,
                                   const QString &linkSpeedText)
{
    strength = qBound(0, strength, 100);
    if (m_state == state
        && m_strength == strength
        && m_interfaceName == interfaceName
        && m_ssid == ssid
        && m_channel == channel
        && m_linkSpeedText == linkSpeedText) {
        return;
    }

    m_state = state;
    m_strength = strength;
    m_interfaceName = interfaceName;
    m_ssid = ssid;
    m_channel = channel;
    m_linkSpeedText = linkSpeedText;
    emit statusChanged();
}

void NetworkManagerModel::refresh()
{
    const QString type = readPrimaryConnectionType();
    const QString connectionPath = readPrimaryConnectionPath();
    const int rootState = readIntProperty(nmService, nmPath, nmRootInterface, QStringLiteral("State"));

    if (connectionPath.isEmpty() || connectionPath == QStringLiteral("/") || rootState < nmStateConnectedLocal) {
        setState(State::Disconnected, 0, {}, {}, 0, {});
        return;
    }

    if (type == QStringLiteral("802-11-wireless")) {
        const QVariant devicesVariant = readProperty(nmService, connectionPath, nmActiveConnectionInterface, QStringLiteral("Devices"));
        const QList<QDBusObjectPath> devices = objectPathListFromVariant(devicesVariant);
        for (const QDBusObjectPath &device : devices) {
            const int deviceType = readIntProperty(nmService, device.path(), nmDeviceInterface, QStringLiteral("DeviceType"));
            if (deviceType == 2) {
                const int frequency = readWirelessFrequency(device.path());
                setState(State::Wireless,
                         readWirelessStrength(device.path()),
                         readStringProperty(nmService, device.path(), nmDeviceInterface, QStringLiteral("Interface")),
                         readWirelessSsid(device.path()),
                         channelFromFrequency(frequency),
                         linkSpeedString(readWirelessBitrate(device.path()) / 1000.0));
                return;
            }
        }
        setState(State::Wireless, 0, {}, {}, 0, {});
        return;
    }

    if (type == QStringLiteral("802-3-ethernet")) {
        const QVariant devicesVariant = readProperty(nmService, connectionPath, nmActiveConnectionInterface, QStringLiteral("Devices"));
        const QList<QDBusObjectPath> devices = objectPathListFromVariant(devicesVariant);
        for (const QDBusObjectPath &device : devices) {
            const int deviceType = readIntProperty(nmService, device.path(), nmDeviceInterface, QStringLiteral("DeviceType"));
            if (deviceType == 1) {
                setState(State::Wired,
                         0,
                         readStringProperty(nmService, device.path(), nmDeviceInterface, QStringLiteral("Interface")),
                         {},
                         0,
                         linkSpeedString(readEthernetSpeed(device.path())));
                return;
            }
        }
        setState(State::Wired, 0, {}, {}, 0, {});
        return;
    }

    setState(State::Disconnected, 0, {}, {}, 0, {});
}
