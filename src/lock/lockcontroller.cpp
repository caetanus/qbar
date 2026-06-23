#include "lockcontroller.h"

#include <QCoreApplication>

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
            if (m_backend != nullptr) {
                m_backend->unlock();
            }
            emit unlocked();
            QCoreApplication::quit();
        });
        connect(m_authenticator, &PamAuthenticator::authenticationFailed, this, [this](const QString &reason) {
            setError(reason);
        });
    }

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
}

void LockController::submitPassword(const QString &password)
{
    setError({});
    if (m_demoMode) {
        emit unlocked();
        QCoreApplication::quit();
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
