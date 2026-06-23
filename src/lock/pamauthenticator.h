#pragma once

#include <QObject>
#include <QString>

class PamAuthenticator : public QObject {
    Q_OBJECT

public:
    explicit PamAuthenticator(QObject *parent = nullptr);

    QString service() const { return m_service; }
    void setService(const QString &service);

    QString user() const { return m_user; }
    void setUser(const QString &user);

    bool busy() const { return m_busy; }

public slots:
    void authenticate(const QString &password);
    void authenticateWithService(const QString &password, const QString &service);

signals:
    void busyChanged();
    void promptChanged(const QString &prompt);
    void messageChanged(const QString &message);
    void authenticationSucceeded();
    void authenticationFailed(const QString &reason);

private:
    void setBusy(bool busy);

    QString m_service = QStringLiteral("qbar-lock");
    QString m_user;
    bool m_busy = false;
    int m_nextAttemptId = 0;
    int m_latestAttemptId = 0;
    int m_activeAttempts = 0;
};
