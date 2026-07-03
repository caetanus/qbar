#pragma once

#include "fprintauthenticator.h"
#include "lockbackend.h"
#include "pamauthenticator.h"

#include <QObject>
#include <QString>

// Coordinates the lock lifecycle and the parallel authentication methods. Password
// (typed), fingerprint (fprintd/D-Bus) and face (a howdy PAM service) run concurrently;
// the FIRST to succeed unlocks and all the others are cancelled. Per-method activity is
// exposed to QML so the lock face can reflect which methods are live.
class LockController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString user READ user CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(QString prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool demoMode READ demoMode CONSTANT)
    Q_PROPERTY(bool fingerprintActive READ fingerprintActive NOTIFY fingerprintActiveChanged)
    Q_PROPERTY(bool faceActive READ faceActive NOTIFY faceActiveChanged)

public:
    explicit LockController(PamAuthenticator *authenticator,
                            LockBackend *backend,
                            bool demoMode,
                            const QString &passwordService,
                            QObject *parent = nullptr);

    // Optional extra methods, wired in by lockmain from CLI/config. A null face
    // authenticator or fingerprintEnabled=false simply leaves that method off.
    void setFaceAuthenticator(PamAuthenticator *face);
    void setFingerprintEnabled(bool enabled) { m_fingerprintEnabled = enabled; }

    QString user() const;
    QString backendName() const;
    QString prompt() const { return m_prompt; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    bool busy() const;
    bool demoMode() const { return m_demoMode; }
    bool fingerprintActive() const { return m_fingerprintActive; }
    bool faceActive() const { return m_faceActive; }

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
    void fingerprintActiveChanged();
    void faceActiveChanged();

private:
    void setPrompt(const QString &prompt);
    void setMessage(const QString &message);
    void setError(const QString &error);
    void setFingerprintActive(bool active);
    void setFaceActive(bool active);
    void startMethods();
    void stopMethods();
    void succeed();

    PamAuthenticator *m_authenticator = nullptr;
    PamAuthenticator *m_face = nullptr;
    FprintAuthenticator m_fingerprint;
    LockBackend *m_backend = nullptr;
    bool m_demoMode = false;
    bool m_fingerprintEnabled = true;
    bool m_faceRunning = false;
    bool m_fingerprintActive = false;
    bool m_faceActive = false;
    bool m_succeeded = false;
    QString m_passwordService;
    // tr() here is evaluated at construction time, after main() installs the
    // translators, so the default prompt is localized like every other string.
    QString m_prompt = tr("Enter password, or use fingerprint / face");
    QString m_message;
    QString m_error;
};
