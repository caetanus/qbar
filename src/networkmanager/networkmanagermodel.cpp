#include "networkmanagermodel.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHostAddress>
#include <QVariantList>
#include <QVariantMap>

namespace {

constexpr auto nmService = "org.freedesktop.NetworkManager";
constexpr auto nmPath = "/org/freedesktop/NetworkManager";
constexpr auto nmRootInterface = "org.freedesktop.NetworkManager";
constexpr auto nmPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto nmActiveConnectionInterface = "org.freedesktop.NetworkManager.Connection.Active";
constexpr auto nmDeviceInterface = "org.freedesktop.NetworkManager.Device";
constexpr auto nmWirelessInterface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto nmAccessPointInterface = "org.freedesktop.NetworkManager.AccessPoint";
constexpr const char *kBusName = "qbar-nm-reader";

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

int channelFromFrequency(int frequency)
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

QString linkSpeedString(double mbps)
{
    if (mbps <= 0.0) {
        return {};
    }
    return QStringLiteral("%1 Mbps").arg(QString::number(mbps, 'f', mbps < 10.0 ? 1 : 0).replace(QStringLiteral(".0"), QString()));
}

} // namespace

// ---------------------------------------------------------------------------
// NmReader (worker thread)
// ---------------------------------------------------------------------------

void NmReader::start()
{
    m_bus = QDBusConnection::connectToBus(QDBusConnection::SystemBus, QString::fromLatin1(kBusName));

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, &NmReader::refresh);

    // Any property change anywhere under the NM service (device, access point,
    // active connection) and root state changes trigger a debounced re-read.
    m_bus.connect(QString::fromLatin1(nmService), QString(), QString::fromLatin1(nmPropertiesInterface),
                  QStringLiteral("PropertiesChanged"), this, SLOT(scheduleRefresh()));
    m_bus.connect(QString::fromLatin1(nmService), QString::fromLatin1(nmPath), QString::fromLatin1(nmRootInterface),
                  QStringLiteral("StateChanged"), this, SLOT(scheduleRefresh()));
    // Refresh when NetworkManager itself (re)appears on the bus.
    m_bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                  this, SLOT(scheduleRefresh()));

    refresh();
}

void NmReader::scheduleRefresh()
{
    if (m_debounce != nullptr) {
        m_debounce->start();
    }
}

QVariant NmReader::readProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    QDBusInterface iface(service, path, QString::fromLatin1(nmPropertiesInterface), m_bus);
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

QString NmReader::readStringProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    return readProperty(service, path, interface, name).toString();
}

int NmReader::readIntProperty(const QString &service, const QString &path, const QString &interface, const QString &name) const
{
    const QVariant value = readProperty(service, path, interface, name);
    return value.isValid() ? value.toInt() : 0;
}

QString NmReader::readPrimaryConnectionType() const
{
    return readStringProperty(QString::fromLatin1(nmService), QString::fromLatin1(nmPath), QString::fromLatin1(nmRootInterface), QStringLiteral("PrimaryConnectionType"));
}

QString NmReader::readPrimaryConnectionPath() const
{
    return objectPathValue(readProperty(QString::fromLatin1(nmService), QString::fromLatin1(nmPath), QString::fromLatin1(nmRootInterface), QStringLiteral("PrimaryConnection")));
}

int NmReader::readWirelessStrength(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmWirelessInterface), QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return 0;
    }
    return qBound(0, readIntProperty(QString::fromLatin1(nmService), accessPointPath, QString::fromLatin1(nmAccessPointInterface), QStringLiteral("Strength")), 100);
}

int NmReader::readWirelessFrequency(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmWirelessInterface), QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return 0;
    }
    return readIntProperty(QString::fromLatin1(nmService), accessPointPath, QString::fromLatin1(nmAccessPointInterface), QStringLiteral("Frequency"));
}

int NmReader::readWirelessBitrate(const QString &devicePath) const
{
    return readIntProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmWirelessInterface), QStringLiteral("Bitrate"));
}

int NmReader::readEthernetSpeed(const QString &devicePath) const
{
    return readIntProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Speed"));
}

QString NmReader::readWirelessSsid(const QString &devicePath) const
{
    const QString accessPointPath = objectPathValue(
        readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmWirelessInterface), QStringLiteral("ActiveAccessPoint")));
    if (accessPointPath.isEmpty() || accessPointPath == QStringLiteral("/")) {
        return {};
    }
    const QVariant value = readProperty(QString::fromLatin1(nmService), accessPointPath, QString::fromLatin1(nmAccessPointInterface), QStringLiteral("Ssid"));
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
    return QString::fromUtf8(bytes.constData(), bytes.size()).trimmed();
}

QString NmReader::readAddressDataFirstIp(const QString &configPath, bool ipv6) const
{
    if (configPath.isEmpty() || configPath == QStringLiteral("/")) {
        return {};
    }
    const QString interfaceName = ipv6
        ? QStringLiteral("org.freedesktop.NetworkManager.IP6Config")
        : QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
    const QVariant value = readProperty(QString::fromLatin1(nmService), configPath, interfaceName, QStringLiteral("AddressData"));
    if (!value.isValid()) {
        return {};
    }

    const auto normalize = [](const QString &address) -> QString {
        const QString trimmed = address.trimmed();
        if (trimmed.isEmpty()) {
            return {};
        }
        const QHostAddress host(trimmed);
        return host.isNull() ? trimmed : host.toString();
    };

    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        for (const QVariant &entry : list) {
            const QString address = normalize(entry.toMap().value(QStringLiteral("address")).toString());
            if (!address.isEmpty()) {
                return address;
            }
        }
    }
    if (value.canConvert<QVariantMap>()) {
        const QString address = normalize(value.toMap().value(QStringLiteral("address")).toString());
        if (!address.isEmpty()) {
            return address;
        }
    }
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        argument.beginArray();
        while (!argument.atEnd()) {
            QVariantMap map;
            argument >> map;
            const QString address = normalize(map.value(QStringLiteral("address")).toString());
            if (!address.isEmpty()) {
                argument.endArray();
                return address;
            }
        }
        argument.endArray();
    }
    return {};
}

QString NmReader::readIpText(const QString &devicePath) const
{
    const QString ip4ConfigPath = objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip4Config")));
    const QString address = readAddressDataFirstIp(ip4ConfigPath, false);
    if (!address.isEmpty()) {
        return address;
    }
    const QString ip6ConfigPath = objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip6Config")));
    return readAddressDataFirstIp(ip6ConfigPath, true);
}

void NmReader::refresh()
{
    NmStatus status;
    const QString type = readPrimaryConnectionType();
    const QString connectionPath = readPrimaryConnectionPath();
    const int rootState = readIntProperty(QString::fromLatin1(nmService), QString::fromLatin1(nmPath), QString::fromLatin1(nmRootInterface), QStringLiteral("State"));

    if (connectionPath.isEmpty() || connectionPath == QStringLiteral("/") || rootState < nmStateConnectedLocal) {
        emit updated(status); // disconnected
        return;
    }

    if (type == QStringLiteral("802-11-wireless")) {
        const QVariant devicesVariant = readProperty(QString::fromLatin1(nmService), connectionPath, QString::fromLatin1(nmActiveConnectionInterface), QStringLiteral("Devices"));
        const QList<QDBusObjectPath> devices = objectPathListFromVariant(devicesVariant);
        for (const QDBusObjectPath &device : devices) {
            if (readIntProperty(QString::fromLatin1(nmService), device.path(), QString::fromLatin1(nmDeviceInterface), QStringLiteral("DeviceType")) != 2) {
                continue;
            }
            const QString devicePath = device.path();
            status.state = 2;
            status.strength = readWirelessStrength(devicePath);
            status.interfaceName = readStringProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Interface"));
            status.ssid = readWirelessSsid(devicePath);
            status.ipText = readIpText(devicePath);
            status.ipv4Text = readAddressDataFirstIp(objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip4Config"))), false);
            status.ipv6Text = readAddressDataFirstIp(objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip6Config"))), true);
            status.channel = channelFromFrequency(readWirelessFrequency(devicePath));
            status.linkSpeedText = linkSpeedString(readWirelessBitrate(devicePath) / 1000.0);
            emit updated(status);
            return;
        }
        status.state = 2;
        emit updated(status);
        return;
    }

    if (type == QStringLiteral("802-3-ethernet")) {
        const QVariant devicesVariant = readProperty(QString::fromLatin1(nmService), connectionPath, QString::fromLatin1(nmActiveConnectionInterface), QStringLiteral("Devices"));
        const QList<QDBusObjectPath> devices = objectPathListFromVariant(devicesVariant);
        for (const QDBusObjectPath &device : devices) {
            if (readIntProperty(QString::fromLatin1(nmService), device.path(), QString::fromLatin1(nmDeviceInterface), QStringLiteral("DeviceType")) != 1) {
                continue;
            }
            const QString devicePath = device.path();
            status.state = 1;
            status.interfaceName = readStringProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Interface"));
            status.ipText = readIpText(devicePath);
            status.ipv4Text = readAddressDataFirstIp(objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip4Config"))), false);
            status.ipv6Text = readAddressDataFirstIp(objectPathValue(readProperty(QString::fromLatin1(nmService), devicePath, QString::fromLatin1(nmDeviceInterface), QStringLiteral("Ip6Config"))), true);
            status.linkSpeedText = linkSpeedString(readEthernetSpeed(devicePath));
            emit updated(status);
            return;
        }
        status.state = 1;
        emit updated(status);
        return;
    }

    emit updated(status); // disconnected
}

// ---------------------------------------------------------------------------
// NetworkManagerModel (GUI thread)
// ---------------------------------------------------------------------------

NetworkManagerModel::NetworkManagerModel(QObject *parent)
    : QObject(parent)
    , m_reader(new NmReader)
{
    qRegisterMetaType<NmStatus>();
    m_reader->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_reader, &NmReader::start);
    connect(m_reader, &NmReader::updated, this, &NetworkManagerModel::apply);
    m_thread.start();
}

NetworkManagerModel::~NetworkManagerModel()
{
    m_thread.quit();
    m_thread.wait();
    delete m_reader;
    QDBusConnection::disconnectFromBus(QStringLiteral("qbar-nm-reader"));
}

void NetworkManagerModel::apply(const NmStatus &status)
{
    const int strength = qBound(0, status.strength, 100);
    if (m_state == status.state
        && m_strength == strength
        && m_interfaceName == status.interfaceName
        && m_ssid == status.ssid
        && m_ipText == status.ipText
        && m_ipv4Text == status.ipv4Text
        && m_ipv6Text == status.ipv6Text
        && m_channel == status.channel
        && m_linkSpeedText == status.linkSpeedText) {
        return;
    }

    m_state = status.state;
    m_strength = strength;
    m_interfaceName = status.interfaceName;
    m_ssid = status.ssid;
    m_ipText = status.ipText;
    m_ipv4Text = status.ipv4Text;
    m_ipv6Text = status.ipv6Text;
    m_channel = status.channel;
    m_linkSpeedText = status.linkSpeedText;
    emit statusChanged();
}

QString NetworkManagerModel::mode() const
{
    return modeName(m_state);
}

QString NetworkManagerModel::iconName() const
{
    if (m_state == 2) {
        return iconNameForStrength(m_strength);
    }
    if (m_state == 1) {
        return QStringLiteral("network-wired-symbolic");
    }
    return QStringLiteral("network-wired-disconnected-symbolic");
}

QString NetworkManagerModel::label() const
{
    if (m_state == 2) {
        return QStringLiteral("%1%").arg(qBound(0, m_strength, 100));
    }
    return {};
}

QString NetworkManagerModel::tooltipText() const
{
    if (m_state == 2) {
        const QString strengthText = QStringLiteral("%1%").arg(qBound(0, m_strength, 100));
        const QString bandText = m_channel > 14 ? QStringLiteral("5G") : QStringLiteral("2G");
        const QString channelText = m_channel > 0 ? QStringLiteral("chan %1").arg(m_channel) : QString();
        const QString linkText = !m_linkSpeedText.isEmpty() ? QStringLiteral("link %1").arg(m_linkSpeedText) : QString();
        const QStringList parts = {m_ssid, m_interfaceName, bandText, channelText, linkText, strengthText};
        QStringList filtered;
        filtered.reserve(parts.size());
        for (const QString &part : parts) {
            if (!part.isEmpty()) {
                filtered.append(part);
            }
        }
        return filtered.join(QStringLiteral(" | "));
    }

    if (m_state == 1) {
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

QString NetworkManagerModel::modeName(int state)
{
    switch (state) {
    case 2:
        return QStringLiteral("wireless");
    case 1:
        return QStringLiteral("wired");
    default:
        return QStringLiteral("disconnected");
    }
}
