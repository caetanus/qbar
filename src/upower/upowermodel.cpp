#include "upowermodel.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

namespace {
constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kRootPath = "/org/freedesktop/UPower";
constexpr auto kManagerInterface = "org.freedesktop.UPower";
constexpr auto kDeviceInterface = "org.freedesktop.UPower.Device";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";

// org.freedesktop.UPower.Device "Type" enum (the values we care to label).
QString deviceKind(uint type)
{
    switch (type) {
    case 5: return QStringLiteral("mouse");
    case 6: return QStringLiteral("keyboard");
    case 8: return QStringLiteral("phone");
    case 9: return QStringLiteral("media-player");
    case 10: return QStringLiteral("tablet");
    case 12: return QStringLiteral("gaming-input");
    case 17: return QStringLiteral("headset");
    case 18: return QStringLiteral("speakers");
    case 19: return QStringLiteral("headphones");
    case 26: return QStringLiteral("wearable");
    default: return QStringLiteral("device");
    }
}
} // namespace

UpowerModel::UpowerModel(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150); // coalesce add/remove/property bursts
    connect(&m_refreshTimer, &QTimer::timeout, this, &UpowerModel::refresh);

    m_watcher = new QDBusServiceWatcher(this);
    m_watcher->setConnection(QDBusConnection::systemBus());
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher->addWatchedService(QString::fromLatin1(kService));
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) {
                    disconnectFromService();
                } else {
                    connectToService();
                }
            });

    connectToService();
}

void UpowerModel::connectToService()
{
    auto *bus = QDBusConnection::systemBus().interface();
    if (bus == nullptr || !bus->isServiceRegistered(QString::fromLatin1(kService))) {
        return;
    }
    if (m_present) {
        return;
    }
    m_present = true;
    m_service = QString::fromLatin1(kService);

    QDBusConnection::systemBus().connect(m_service, QString::fromLatin1(kRootPath),
                                         QString::fromLatin1(kManagerInterface),
                                         QStringLiteral("DeviceAdded"), this,
                                         SLOT(handleDeviceListChanged()));
    QDBusConnection::systemBus().connect(m_service, QString::fromLatin1(kRootPath),
                                         QString::fromLatin1(kManagerInterface),
                                         QStringLiteral("DeviceRemoved"), this,
                                         SLOT(handleDeviceListChanged()));
    refresh();
}

void UpowerModel::disconnectFromService()
{
    m_present = false;
    m_service.clear();
    m_deviceData.clear();
    m_watchedPaths.clear();
    if (!m_devices.isEmpty()) {
        m_devices.clear();
        emit changed();
    }
}

void UpowerModel::handleDeviceListChanged()
{
    scheduleRefresh();
}

void UpowerModel::scheduleRefresh()
{
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void UpowerModel::refresh()
{
    if (m_service.isEmpty()) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(m_service, QString::fromLatin1(kRootPath),
                                               QString::fromLatin1(kManagerInterface),
                                               QStringLiteral("EnumerateDevices"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QList<QDBusObjectPath>> reply = *w;
        if (reply.isError()) {
            return;
        }
        QSet<QString> live;
        const QList<QDBusObjectPath> paths = reply.value();
        for (const QDBusObjectPath &p : paths) {
            live.insert(p.path());
        }
        // Drop devices that vanished.
        for (auto it = m_deviceData.begin(); it != m_deviceData.end();) {
            if (!live.contains(it.key())) {
                it = m_deviceData.erase(it);
            } else {
                ++it;
            }
        }
        // Fetch each device's properties (async); rebuild the list as each returns.
        for (const QString &path : std::as_const(live)) {
            watchDevice(path);
            auto getAll = QDBusMessage::createMethodCall(m_service, path,
                                                         QString::fromLatin1(kPropsInterface),
                                                         QStringLiteral("GetAll"));
            getAll << QString::fromLatin1(kDeviceInterface);
            auto *dw = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(getAll), this);
            connect(dw, &QDBusPendingCallWatcher::finished, this,
                    [this, path](QDBusPendingCallWatcher *dwInner) {
                        dwInner->deleteLater();
                        const QDBusPendingReply<QVariantMap> propsReply = *dwInner;
                        if (propsReply.isError()) {
                            return;
                        }
                        m_deviceData.insert(path, propsReply.value());
                        rebuildDevices();
                    });
        }
        // If everything is gone, reflect that immediately.
        if (live.isEmpty()) {
            rebuildDevices();
        }
    });
}

void UpowerModel::watchDevice(const QString &path)
{
    if (m_watchedPaths.contains(path)) {
        return;
    }
    m_watchedPaths.insert(path);
    QDBusConnection::systemBus().connect(m_service, path, QString::fromLatin1(kPropsInterface),
                                         QStringLiteral("PropertiesChanged"), this,
                                         SLOT(handleDevicePropertiesChanged(QString, QVariantMap, QStringList)));
}

void UpowerModel::handleDevicePropertiesChanged(const QString &interface,
                                                const QVariantMap &,
                                                const QStringList &)
{
    if (interface == QLatin1String(kDeviceInterface)) {
        scheduleRefresh();
    }
}

void UpowerModel::rebuildDevices()
{
    QVariantList out;
    for (auto it = m_deviceData.constBegin(); it != m_deviceData.constEnd(); ++it) {
        const QVariantMap &p = it.value();
        // Peripherals only: the laptop battery / AC line are PowerSupply=true and belong to
        // the Battery applet. Keep present, non-power-supply devices.
        if (p.value(QStringLiteral("PowerSupply")).toBool()
            || !p.value(QStringLiteral("IsPresent")).toBool()) {
            continue;
        }
        const uint type = p.value(QStringLiteral("Type")).toUInt();
        if (type <= 1) { // 0 unknown, 1 line-power: never a peripheral battery
            continue;
        }
        const uint state = p.value(QStringLiteral("State")).toUInt();
        QVariantMap d;
        d.insert(QStringLiteral("path"), it.key());
        d.insert(QStringLiteral("percentage"), qRound(p.value(QStringLiteral("Percentage")).toDouble()));
        d.insert(QStringLiteral("state"), state);
        d.insert(QStringLiteral("isCharging"), state == 1 || state == 5); // charging / pending-charge
        d.insert(QStringLiteral("iconName"), p.value(QStringLiteral("IconName")).toString());
        d.insert(QStringLiteral("model"), p.value(QStringLiteral("Model")).toString());
        d.insert(QStringLiteral("type"), type);
        d.insert(QStringLiteral("kind"), deviceKind(type));
        out.append(d);
    }
    // Stable order by device path so icons don't jump around between refreshes.
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("path")).toString()
             < b.toMap().value(QStringLiteral("path")).toString();
    });
    m_devices = out;
    emit changed();
}

QString UpowerModel::tooltipText() const
{
    if (m_devices.isEmpty()) {
        return QStringLiteral("No peripheral batteries");
    }
    QStringList lines;
    for (const QVariant &v : m_devices) {
        const QVariantMap d = v.toMap();
        QString name = d.value(QStringLiteral("model")).toString();
        if (name.isEmpty()) {
            name = d.value(QStringLiteral("kind")).toString();
        }
        QString line = QStringLiteral("%1: %2%").arg(name).arg(d.value(QStringLiteral("percentage")).toInt());
        if (d.value(QStringLiteral("isCharging")).toBool()) {
            line += QStringLiteral(" (charging)");
        }
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}
