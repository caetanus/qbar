#include "failedunitsmodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
constexpr auto kService = "org.freedesktop.systemd1";
constexpr auto kPath = "/org/freedesktop/systemd1";
constexpr auto kManager = "org.freedesktop.systemd1.Manager";

// One entry of ListUnitsFiltered's a(ssssssouso).
struct SdUnit {
    QString name;
    QString description;
    QString loadState;
    QString activeState;
    QString subState;
    QString following;
    QDBusObjectPath path;
    uint jobId = 0;
    QString jobType;
    QDBusObjectPath jobPath;
};

const QDBusArgument &operator>>(const QDBusArgument &arg, SdUnit &unit)
{
    arg.beginStructure();
    arg >> unit.name >> unit.description >> unit.loadState >> unit.activeState >> unit.subState
        >> unit.following >> unit.path >> unit.jobId >> unit.jobType >> unit.jobPath;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const SdUnit &unit)
{
    arg.beginStructure();
    arg << unit.name << unit.description << unit.loadState << unit.activeState << unit.subState
        << unit.following << unit.path << unit.jobId << unit.jobType << unit.jobPath;
    arg.endStructure();
    return arg;
}

QDBusConnection busFor(bool systemBus)
{
    return systemBus ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
}

} // namespace

using SdUnitList = QList<SdUnit>;
Q_DECLARE_METATYPE(SdUnit)
Q_DECLARE_METATYPE(SdUnitList)

FailedUnitsModel::FailedUnitsModel(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<SdUnit>();
    qDBusRegisterMetaType<SdUnitList>();

    // Several manager signals can land in a burst (a unit failing tears through
    // multiple jobs); coalesce them into one query round.
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(300);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &FailedUnitsModel::refresh);

    // Failsafe for transitions without a job (watchdog/OOM kills).
    m_failsafe.setInterval(120000);
    connect(&m_failsafe, &QTimer::timeout, this, &FailedUnitsModel::refresh);
    m_failsafe.start();

    wireBus(true);
    wireBus(false);
    refresh();
}

void FailedUnitsModel::wireBus(bool systemBus)
{
    QDBusConnection bus = busFor(systemBus);
    if (!bus.isConnected()) {
        return;
    }
    // Managers only emit their signals to explicit subscribers.
    bus.asyncCall(QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                                 QString::fromLatin1(kPath),
                                                 QString::fromLatin1(kManager),
                                                 QStringLiteral("Subscribe")));
    // A slot taking fewer arguments than the signal is fine for QtDBus; both
    // signals just mean "unit states may have changed".
    bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                QString::fromLatin1(kManager), QStringLiteral("JobRemoved"), this,
                SLOT(scheduleRefresh()));
    bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                QString::fromLatin1(kManager), QStringLiteral("Reloading"), this,
                SLOT(scheduleRefresh()));
    // reset-failed drops units without creating a job — no JobRemoved — but it
    // does remove them from the manager's unit list.
    bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                QString::fromLatin1(kManager), QStringLiteral("UnitRemoved"), this,
                SLOT(scheduleRefresh()));
}

void FailedUnitsModel::scheduleRefresh()
{
    m_refreshDebounce.start();
}

void FailedUnitsModel::refresh()
{
    refreshBus(true);
    refreshBus(false);
}

void FailedUnitsModel::refreshBus(bool systemBus)
{
    QDBusConnection bus = busFor(systemBus);
    if (!bus.isConnected()) {
        applyBusUnits(systemBus, {});
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                                       QString::fromLatin1(kPath),
                                                       QString::fromLatin1(kManager),
                                                       QStringLiteral("ListUnitsFiltered"));
    call << QStringList{QStringLiteral("failed")};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, systemBus](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                QDBusPendingReply<SdUnitList> reply = *w;
                if (reply.isError()) {
                    applyBusUnits(systemBus, {});
                    return;
                }
                QVariantList units;
                const SdUnitList list = reply.value();
                for (const SdUnit &u : list) {
                    units.append(QVariantMap{
                        {QStringLiteral("name"), u.name},
                        {QStringLiteral("description"), u.description},
                        {QStringLiteral("activeState"), u.activeState},
                        {QStringLiteral("subState"), u.subState},
                        {QStringLiteral("scope"),
                         systemBus ? QStringLiteral("system") : QStringLiteral("user")},
                    });
                }
                applyBusUnits(systemBus, units);
            });
}

void FailedUnitsModel::applyBusUnits(bool systemBus, const QVariantList &units)
{
    QVariantList &slot = systemBus ? m_systemUnits : m_userUnits;
    if (slot == units && m_available) {
        return;
    }
    slot = units;
    rebuild();
}

void FailedUnitsModel::rebuild()
{
    m_available = true;
    m_units = m_systemUnits + m_userUnits;
    if (m_units.isEmpty()) {
        m_tooltipText = tr("No failed systemd units");
    } else {
        QStringList lines;
        lines.reserve(m_units.size());
        for (const QVariant &v : std::as_const(m_units)) {
            const QVariantMap u = v.toMap();
            lines.append(QStringLiteral("%1 (%2)").arg(u.value(QStringLiteral("name")).toString(),
                                                       u.value(QStringLiteral("scope")).toString()));
        }
        m_tooltipText = tr("%n failed unit(s)", nullptr, static_cast<int>(m_units.size()))
            + QStringLiteral("\n") + lines.join(QLatin1Char('\n'));
    }
    emit changed();
}
