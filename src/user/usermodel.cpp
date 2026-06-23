#include "usermodel.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <pwd.h>
#include <unistd.h>

namespace {
constexpr auto kAccountsService = "org.freedesktop.Accounts";
constexpr auto kAccountsPath = "/org/freedesktop/Accounts";
constexpr auto kAccountsInterface = "org.freedesktop.Accounts";
constexpr auto kUserInterface = "org.freedesktop.Accounts.User";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";
} // namespace

UserModel::UserModel(QObject *parent)
    : QObject(parent)
{
    resolveIdentity();
    queryAccountsService(); // canonical avatar/real-name; overrides the fallbacks when it answers

    // System uptime ticks every minute (the display granularity is minutes).
    m_timer.setInterval(60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &UserModel::refreshUptime);
    m_timer.start();
    refreshUptime();
}

void UserModel::resolveIdentity()
{
    const struct passwd *pw = getpwuid(getuid());
    if (pw != nullptr) {
        m_userName = QString::fromLocal8Bit(pw->pw_name);
        // GECOS: the real name is the part before the first comma.
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos);
        m_realName = gecos.section(QLatin1Char(','), 0, 0).trimmed();
    }
    if (m_userName.isEmpty()) {
        m_userName = qEnvironmentVariable("USER");
    }
    if (m_realName.isEmpty()) {
        m_realName = m_userName;
    }

    // Fallback avatar only: the conventional per-user faces. The canonical source is
    // AccountsService (queryAccountsService), which overrides this when it answers.
    const QString home = pw != nullptr ? QString::fromLocal8Bit(pw->pw_dir) : QDir::homePath();
    const QStringList candidates{
        home + QStringLiteral("/.face"),
        home + QStringLiteral("/.face.icon"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            m_iconPath = QStringLiteral("file://") + path;
            break;
        }
    }
}

void UserModel::queryAccountsService()
{
    // Canonical user metadata: ask org.freedesktop.Accounts for this uid's object, then read
    // its IconFile / RealName. All async — AccountsService may be absent (fall back silently).
    auto find = QDBusMessage::createMethodCall(QString::fromLatin1(kAccountsService),
                                               QString::fromLatin1(kAccountsPath),
                                               QString::fromLatin1(kAccountsInterface),
                                               QStringLiteral("FindUserById"));
    find << static_cast<qlonglong>(getuid());
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(find), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            return;
        }
        applyUserObject(reply.value().path());
    });
}

void UserModel::applyUserObject(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    auto getAll = QDBusMessage::createMethodCall(QString::fromLatin1(kAccountsService), path,
                                                 QString::fromLatin1(kPropsInterface),
                                                 QStringLiteral("GetAll"));
    getAll << QString::fromLatin1(kUserInterface);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(getAll), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            return;
        }
        const QVariantMap props = reply.value();
        bool touched = false;
        const QString realName = props.value(QStringLiteral("RealName")).toString();
        if (!realName.isEmpty() && realName != m_realName) {
            m_realName = realName;
            touched = true;
        }
        const QString iconFile = props.value(QStringLiteral("IconFile")).toString();
        if (!iconFile.isEmpty() && QFileInfo::exists(iconFile)) {
            const QString url = QStringLiteral("file://") + iconFile;
            if (url != m_iconPath) {
                m_iconPath = url;
                touched = true;
            }
        }
        if (touched) {
            emit changed();
        }
    });
}

void UserModel::refreshUptime()
{
    QFile file(QStringLiteral("/proc/uptime"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    // "/proc/uptime" is "<seconds-up> <seconds-idle>".
    const QByteArray first = file.readLine().split(' ').value(0);
    const int seconds = static_cast<int>(first.toDouble());
    if (seconds != m_uptimeSeconds) {
        m_uptimeSeconds = seconds;
        emit changed();
    }
}

void UserModel::refreshSessions()
{
    // `who` lists the logged-in sessions; run it off the GUI thread and publish on finish.
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc](int, QProcess::ExitStatus) {
                const QString out = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (out != m_sessionsText) {
                    m_sessionsText = out;
                    emit changed();
                }
            });
    connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
    proc->start(QStringLiteral("who"), QStringList());
}

QString UserModel::uptimeText() const
{
    const int total = m_uptimeSeconds;
    const int days = total / 86400;
    const int hours = (total % 86400) / 3600;
    const int minutes = (total % 3600) / 60;
    if (days > 0) {
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    return QStringLiteral("%1m").arg(minutes);
}

QString UserModel::tooltipText() const
{
    QString who = m_realName.isEmpty() ? m_userName : m_realName;
    if (!m_realName.isEmpty() && m_realName != m_userName) {
        who += QStringLiteral(" (%1)").arg(m_userName);
    }
    return QStringLiteral("%1\nUp %2").arg(who, uptimeText());
}
