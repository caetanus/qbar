#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// waybar's "user" module: the current user's name, an avatar path (~/.face, AccountsService…),
// and the system uptime (refreshed on a timer). Avatar/name are resolved once at startup.
class UserModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName CONSTANT)
    // realName/iconPath come from AccountsService (canonical, async) and fall back to the
    // passwd GECOS / ~/.face — so they can change after construction.
    Q_PROPERTY(QString realName READ realName NOTIFY changed)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY changed)  // "file://…" or empty
    Q_PROPERTY(int uptimeSeconds READ uptimeSeconds NOTIFY changed)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)
    // Logged-in sessions (the `who` output); populated on demand by refreshSessions().
    Q_PROPERTY(QString sessionsText READ sessionsText NOTIFY changed)

public:
    explicit UserModel(QObject *parent = nullptr);

    QString userName() const { return m_userName; }
    QString realName() const { return m_realName; }
    QString iconPath() const { return m_iconPath; }
    int uptimeSeconds() const { return m_uptimeSeconds; }
    QString uptimeText() const;
    QString tooltipText() const;
    QString sessionsText() const { return m_sessionsText; }

    // Run `who` asynchronously and refresh sessionsText (called when the popup opens).
    Q_INVOKABLE void refreshSessions();

signals:
    void changed();

private:
    void resolveIdentity();
    void queryAccountsService();  // canonical avatar/real-name via org.freedesktop.Accounts
    void applyUserObject(const QString &path);
    void refreshUptime();

    QString m_userName;
    QString m_realName;
    QString m_iconPath;
    QString m_sessionsText;
    int m_uptimeSeconds = 0;
    QTimer m_timer;
};
