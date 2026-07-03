#include "lockcontroller.h"

#include <QCoreApplication>
#include <QTimer>

namespace {
// Delay before re-arming a face (howdy) attempt after it fails, so we don't spin the
// camera flat out. Fingerprint re-arms itself via fprintd's VerifyStatus.
constexpr int kFaceRetryMs = 1500;
// How long a rejected fingerprint scan stays in the (red, shaking) error state before
// reverting to the neutral "touch the reader" hint — the reader has already re-armed.
constexpr int kFingerprintErrorMs = 2500;
} // namespace

LockController::LockController(PamAuthenticator *authenticator,
                               LockBackend *backend,
                               bool demoMode,
                               const QString &passwordService,
                               QObject *parent)
    : QObject(parent)
    , m_authenticator(authenticator)
    , m_backend(backend)
    , m_demoMode(demoMode)
    , m_passwordService(passwordService.trimmed())
{
    if (m_authenticator != nullptr) {
        connect(m_authenticator, &PamAuthenticator::busyChanged, this, &LockController::busyChanged);
        connect(m_authenticator, &PamAuthenticator::promptChanged, this, [this](const QString &prompt) {
            setPrompt(prompt);
        });
        connect(m_authenticator, &PamAuthenticator::messageChanged, this, [this](const QString &message) {
            setMessage(message);
        });
        connect(m_authenticator, &PamAuthenticator::authenticationSucceeded, this, [this]() {
            succeed();
        });
        connect(m_authenticator, &PamAuthenticator::authenticationFailed, this, [this](const QString &reason) {
            setError(reason);
        });
    }

    // Fingerprint (fprintd/D-Bus): wins immediately on match; cancelled on any other win.
    connect(&m_fingerprint, &FprintAuthenticator::authenticationSucceeded, this, [this]() {
        succeed();
    });
    connect(&m_fingerprint, &FprintAuthenticator::activeChanged, this, [this](bool active) {
        setFingerprintActive(active);
    });
    connect(&m_fingerprint, &FprintAuthenticator::statusChanged, this, [this](const QString &status) {
        if (!m_succeeded) {
            setMessage(status);
        }
    });
    // A rejected scan is a real failure — route it through the ERROR channel so both
    // faces react loudly (shake + red border/ring), not just the quiet hint line.
    // Clear-then-set guarantees an errorChanged per rejection (identical consecutive
    // messages would otherwise be deduplicated and only shake once). The reader has
    // already re-armed, so the error state auto-reverts to the hint — unless another
    // failure (e.g. a wrong password) replaced it meanwhile.
    connect(&m_fingerprint, &FprintAuthenticator::scanFailed, this, [this](const QString &reason) {
        if (m_succeeded) {
            return;
        }
        setError({});
        setError(reason);
        QTimer::singleShot(kFingerprintErrorMs, this, [this, reason]() {
            if (m_error == reason) {
                setError({});
            }
        });
    });

    if (m_backend != nullptr) {
        connect(m_backend, &LockBackend::locked, this, [this]() {
            setMessage(QStringLiteral("Locked with %1").arg(m_backend->name()));
        });
        connect(m_backend, &LockBackend::lockFailed, this, [this](const QString &reason) {
            setError(reason);
            emit fatalError(reason);
            if (!m_demoMode) {
                QCoreApplication::exit(2);
            }
        });
    }
}

void LockController::setFaceAuthenticator(PamAuthenticator *face)
{
    m_face = face;
    if (m_face == nullptr) {
        return;
    }
    // Only the pass/fail outcome matters for face — its prompts/messages must not clobber
    // the primary (password) prompt line.
    connect(m_face, &PamAuthenticator::authenticationSucceeded, this, [this]() {
        succeed();
    });
    connect(m_face, &PamAuthenticator::authenticationFailed, this, [this]() {
        if (!m_faceRunning || m_succeeded) {
            return;
        }
        // Re-arm: keep watching the camera until another method wins.
        QTimer::singleShot(kFaceRetryMs, this, [this]() {
            if (m_faceRunning && !m_succeeded && m_face != nullptr) {
                m_face->authenticate(QString());
            }
        });
    });
}

QString LockController::user() const
{
    return m_authenticator != nullptr ? m_authenticator->user() : QString();
}

QString LockController::backendName() const
{
    return m_backend != nullptr ? m_backend->name() : QStringLiteral("none");
}

bool LockController::busy() const
{
    return m_authenticator != nullptr && m_authenticator->busy();
}

void LockController::start()
{
    if (m_demoMode) {
        setMessage(QStringLiteral("Demo mode"));
        startMethods(); // exercise fingerprint/face wiring in demo too, but never auto-unlock
        return;
    }
    if (m_backend == nullptr) {
        emit fatalError(QStringLiteral("No lock backend configured"));
        QCoreApplication::exit(2);
        return;
    }
    if (!m_backend->isAvailable()) {
        emit fatalError(m_backend->unavailableReason());
        QCoreApplication::exit(2);
        return;
    }
    m_backend->lock();
    startMethods();
}

void LockController::startMethods()
{
    // Fingerprint: start the continuous fprintd verify loop if a reader is present.
    if (m_fingerprintEnabled && FprintAuthenticator::isAvailable()) {
        m_fingerprint.setUser(user());
        m_fingerprint.start();
    }
    // Face: kick the first howdy attempt; it re-arms via authenticationFailed.
    if (m_face != nullptr) {
        m_faceRunning = true;
        setFaceActive(true);
        m_face->authenticate(QString());
    }
}

void LockController::stopMethods()
{
    m_faceRunning = false;
    setFaceActive(false);
    m_fingerprint.stop();
    setFingerprintActive(false);
}

void LockController::succeed()
{
    if (m_succeeded) {
        return; // first method already won
    }
    m_succeeded = true;
    stopMethods();
    setMessage(QStringLiteral("Unlocked"));
    if (m_demoMode) {
        emit unlocked();
        QCoreApplication::quit();
        return;
    }
    if (m_backend != nullptr) {
        m_backend->unlock();
    }
    emit unlocked();
    QCoreApplication::quit();
}

void LockController::submitPassword(const QString &password)
{
    setError({});
    if (m_demoMode) {
        succeed();
        return;
    }
    if (m_authenticator != nullptr) {
        if (!password.isEmpty() && !m_passwordService.isEmpty()) {
            m_authenticator->authenticateWithService(password, m_passwordService);
        } else {
            m_authenticator->authenticate(password);
        }
    }
}

void LockController::cancel()
{
    if (m_demoMode) {
        stopMethods();
        QCoreApplication::quit();
    }
}

void LockController::setPrompt(const QString &prompt)
{
    if (m_prompt == prompt) {
        return;
    }
    m_prompt = prompt;
    emit promptChanged();
}

void LockController::setMessage(const QString &message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    emit messageChanged();
}

void LockController::setError(const QString &error)
{
    if (m_error == error) {
        return;
    }
    m_error = error;
    emit errorChanged();
}

void LockController::setFingerprintActive(bool active)
{
    if (m_fingerprintActive == active) {
        return;
    }
    m_fingerprintActive = active;
    emit fingerprintActiveChanged();
}

void LockController::setFaceActive(bool active)
{
    if (m_faceActive == active) {
        return;
    }
    m_faceActive = active;
    emit faceActiveChanged();
}
