#pragma once

#include <QObject>
#include <QString>

class QDBusObjectPath;

// Fingerprint authentication via the fprintd D-Bus API (net.reactivated.Fprint) on the
// system bus. We talk to fprintd directly instead of pam_fprintd so verification can be
// cancelled cleanly (VerifyStop + Release) the instant another method wins — pam_fprintd
// blocks in a PAM conversation and cannot be interrupted.
//
// Flow: GetDefaultDevice -> Claim(user) -> VerifyStart("any"), then react to VerifyStatus
// signals. A non-matching "done" restarts verification (re-arm) so the reader keeps
// listening; a match emits authenticationSucceeded once.
class FprintAuthenticator : public QObject {
    Q_OBJECT

public:
    explicit FprintAuthenticator(QObject *parent = nullptr);
    ~FprintAuthenticator() override;

    // True if fprintd is on the bus and exposes a default device. Cheap; safe to call
    // before start() to decide whether to advertise fingerprint as a method.
    static bool isAvailable();

    void setUser(const QString &user) { m_user = user; }
    bool active() const { return m_active; }

public slots:
    void start();
    void stop();

signals:
    void authenticationSucceeded();
    void activeChanged(bool active);
    void statusChanged(const QString &status);
    void unavailable(const QString &reason);

private slots:
    void handleVerifyStatus(const QString &result, bool done);

private:
    bool resolveDevice();
    void beginVerify();
    void endVerify();
    void setActive(bool active);

    QString m_user;
    QString m_devicePath;
    bool m_claimed = false;
    bool m_verifying = false;
    bool m_running = false; // user intent: keep re-arming until stop()
    bool m_active = false;  // reader currently listening
    bool m_signalsConnected = false;
};
