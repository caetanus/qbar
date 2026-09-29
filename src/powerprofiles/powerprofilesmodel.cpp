#include "powerprofilesmodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace {
constexpr auto kLegacyService = "net.hadess.PowerProfiles";
constexpr auto kLegacyPath = "/net/hadess/PowerProfiles";
constexpr auto kLegacyInterface = "net.hadess.PowerProfiles";
constexpr auto kUPowerService = "org.freedesktop.UPower.PowerProfiles";
constexpr auto kUPowerPath = "/org/freedesktop/UPower/PowerProfiles";
// The renamed service exposes its properties under the matching interface name, NOT
// net.hadess.PowerProfiles — GetAll/Set/PropertiesChanged must use the right one per service.
constexpr auto kUPowerInterface = "org.freedesktop.UPower.PowerProfiles";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";

#ifndef QBAR_CPU_BOOST_HELPER
#define QBAR_CPU_BOOST_HELPER "/usr/lib/qbar-cpu-boost"
#endif
constexpr auto kIntelNoTurbo = "/sys/devices/system/cpu/intel_pstate/no_turbo";
constexpr auto kCpufreqBoost = "/sys/devices/system/cpu/cpufreq/boost";
constexpr auto kPolicy0Boost = "/sys/devices/system/cpu/cpufreq/policy0/boost";

// "0"/"1" from a sysfs knob, or -1 when it doesn't exist / can't be read.
int readSysfsFlag(const char *path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray value = file.readAll().trimmed();
    return value == "1" ? 1 : (value == "0" ? 0 : -1);
}
} // namespace

// "Profiles" is aa{sv}; register the typed list so qdbus_cast can demarshal it
// with QtDBus' streaming operators (manual QDBusArgument iteration on the
// read-only value triggers "write from a read-only object").
using PpdProfileList = QList<QVariantMap>;
Q_DECLARE_METATYPE(PpdProfileList)

PowerProfilesModel::PowerProfilesModel(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PpdProfileList>();

    // Watch both well-known names; whichever owns first wins. The interface name
    // stayed net.hadess.PowerProfiles even after the service was renamed.
    m_watcher = new QDBusServiceWatcher(this);
    m_watcher->setConnection(QDBusConnection::systemBus());
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher->addWatchedService(QString::fromLatin1(kLegacyService));
    m_watcher->addWatchedService(QString::fromLatin1(kUPowerService));
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) {
                    disconnectFromService();
                } else {
                    connectToService();
                }
            });

    connectToService();
    refreshBoost();
}

bool PowerProfilesModel::boostWritable() const
{
    return m_boostSupported && QFileInfo(QStringLiteral(QBAR_CPU_BOOST_HELPER)).isExecutable();
}

void PowerProfilesModel::refreshBoost()
{
    // Same precedence as the helper: intel_pstate's inverted knob first.
    int value = readSysfsFlag(kIntelNoTurbo);
    if (value >= 0) {
        value = 1 - value;
    } else {
        value = readSysfsFlag(kCpufreqBoost);
        if (value < 0) {
            value = readSysfsFlag(kPolicy0Boost);
        }
    }
    const bool supported = value >= 0;
    const bool boost = value == 1;
    if (supported != m_boostSupported || boost != m_boost) {
        m_boostSupported = supported;
        m_boost = boost;
        emit boostChanged();
    }
}

void PowerProfilesModel::setBoost(bool enabled)
{
    if (!boostWritable() || m_boostPending || enabled == m_boost) {
        return;
    }
    m_boostPending = true;
    m_boostError.clear();
    emit boostChanged();

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        m_boostPending = false;
        if (status != QProcess::NormalExit || exitCode != 0) {
            // pkexec: 126 = dismissed / not authorized, 127 = auth failed.
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
            m_boostError = (exitCode == 126 || exitCode == 127)
                ? tr("Not authorized to change CPU boost")
                : (stderrText.isEmpty() ? tr("Could not change CPU boost") : stderrText);
        }
        process->deleteLater();
        emit boostChanged();
        refreshBoost();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        m_boostPending = false;
        m_boostError = tr("pkexec is not available");
        process->deleteLater();
        emit boostChanged();
    });
    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral(QBAR_CPU_BOOST_HELPER),
                    enabled ? QStringLiteral("1") : QStringLiteral("0")});
}

void PowerProfilesModel::connectToService()
{
    auto *bus = QDBusConnection::systemBus().interface();
    if (bus == nullptr) {
        return;
    }
    // Pick the registered name (prefer the modern UPower one).
    QString service;
    QString path;
    QString interface;
    if (bus->isServiceRegistered(QString::fromLatin1(kUPowerService))) {
        service = QString::fromLatin1(kUPowerService);
        path = QString::fromLatin1(kUPowerPath);
        interface = QString::fromLatin1(kUPowerInterface);
    } else if (bus->isServiceRegistered(QString::fromLatin1(kLegacyService))) {
        service = QString::fromLatin1(kLegacyService);
        path = QString::fromLatin1(kLegacyPath);
        interface = QString::fromLatin1(kLegacyInterface);
    } else {
        return;
    }

    if (m_service == service && m_available) {
        return;
    }
    m_service = service;
    m_path = path;
    m_interface = interface;

    QDBusConnection::systemBus().connect(m_service, m_path, QString::fromLatin1(kPropsInterface),
                                         QStringLiteral("PropertiesChanged"), this,
                                         SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));
    refresh();
}

void PowerProfilesModel::disconnectFromService()
{
    if (m_service.isEmpty()) {
        return;
    }
    QDBusConnection::systemBus().disconnect(m_service, m_path, QString::fromLatin1(kPropsInterface),
                                            QStringLiteral("PropertiesChanged"), this,
                                            SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));
    m_service.clear();
    m_path.clear();
    m_interface.clear();
    m_available = false;
    m_activeProfile.clear();
    m_profiles.clear();
    emit changed();
}

void PowerProfilesModel::refresh()
{
    if (m_service.isEmpty()) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(m_service, m_path, QString::fromLatin1(kPropsInterface),
                                               QStringLiteral("GetAll"));
    call << m_interface;
    auto pending = QDBusConnection::systemBus().asyncCall(call);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            return;
        }
        const QVariantMap props = reply.value();
        applyProfiles(props.value(QStringLiteral("Profiles")));
        m_available = true;
        applyActiveProfile(props.value(QStringLiteral("ActiveProfile")).toString());
        emit changed();
    });
}

void PowerProfilesModel::handlePropertiesChanged(const QString &interface,
                                                 const QVariantMap &changedProps,
                                                 const QStringList &)
{
    if (interface != m_interface) {
        return;
    }
    bool touched = false;
    if (changedProps.contains(QStringLiteral("Profiles"))) {
        applyProfiles(changedProps.value(QStringLiteral("Profiles")));
        touched = true;
    }
    if (changedProps.contains(QStringLiteral("ActiveProfile"))) {
        applyActiveProfile(changedProps.value(QStringLiteral("ActiveProfile")).toString());
        touched = true;
    }
    if (touched) {
        m_available = true;
        emit changed();
    }
}

void PowerProfilesModel::applyActiveProfile(const QString &profile)
{
    m_activeProfile = profile;
}

void PowerProfilesModel::applyProfiles(const QVariant &profilesVariant)
{
    // "Profiles" is an array of a{sv}; each entry has a "Profile" string.
    if (!profilesVariant.canConvert<QDBusArgument>()) {
        return;
    }
    const PpdProfileList entries = qdbus_cast<PpdProfileList>(profilesVariant.value<QDBusArgument>());
    QStringList names;
    for (const QVariantMap &entry : entries) {
        const QString name = entry.value(QStringLiteral("Profile")).toString();
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    if (!names.isEmpty()) {
        m_profiles = names;
    }
}

void PowerProfilesModel::setProfile(const QString &profile)
{
    if (m_service.isEmpty() || profile.isEmpty() || profile == m_activeProfile) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(m_service, m_path, QString::fromLatin1(kPropsInterface),
                                               QStringLiteral("Set"));
    call << m_interface << QStringLiteral("ActiveProfile")
         << QVariant::fromValue(QDBusVariant(profile));
    QDBusConnection::systemBus().asyncCall(call);
}

void PowerProfilesModel::cycle()
{
    if (m_profiles.isEmpty()) {
        return;
    }
    const qsizetype current = m_profiles.indexOf(m_activeProfile);
    const qsizetype next = (current + 1) % m_profiles.size();
    setProfile(m_profiles.at(next));
}

QString PowerProfilesModel::displayText() const
{
    if (m_activeProfile == QLatin1String("performance")) {
        return QStringLiteral("Perf");
    }
    if (m_activeProfile == QLatin1String("power-saver")) {
        return QStringLiteral("Saver");
    }
    if (m_activeProfile == QLatin1String("balanced")) {
        return QStringLiteral("Balanced");
    }
    return m_activeProfile;
}

QString PowerProfilesModel::tooltipText() const
{
    if (!m_available) {
        return QStringLiteral("power-profiles-daemon unavailable");
    }
    return QStringLiteral("Power profile: %1").arg(m_activeProfile);
}
