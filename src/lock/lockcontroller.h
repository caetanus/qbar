#pragma once

#include "lockbackend.h"
#include "pamauthenticator.h"

#include <QObject>
#include <QString>

class LockController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString user READ user CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(QString prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool demoMode READ demoMode CONSTANT)

public:
    explicit LockController(PamAuthenticator *authenticator,
                            LockBackend *backend,
                            bool demoMode,
                            const QString &passwordService,
                            QObject *parent = nullptr);

    QString user() const;
    QString backendName() const;
    QString prompt() const { return m_prompt; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    bool busy() const;
    bool demoMode() const { return m_demoMode; }

public slots:
    void start();
    void submitPassword(const QString &password);
    void cancel();

signals:
    void promptChanged();
    void messageChanged();
    void errorChanged();
    void busyChanged();
    void unlocked();
    void fatalError(const QString &reason);

private:
    void setPrompt(const QString &prompt);
    void setMessage(const QString &message);
    void setError(const QString &error);

    PamAuthenticator *m_authenticator = nullptr;
    LockBackend *m_backend = nullptr;
    bool m_demoMode = false;
    QString m_passwordService;
    QString m_prompt = QStringLiteral("Enter password or touch fingerprint reader");
    QString m_message;
    QString m_error;
};
