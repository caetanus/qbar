#include "pamauthenticator.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <security/pam_appl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

struct PamConversationData {
    QByteArray password;
};

char *dupCString(const QByteArray &text)
{
    char *copy = static_cast<char *>(std::calloc(static_cast<size_t>(text.size()) + 1, sizeof(char)));
    if (copy == nullptr) {
        return nullptr;
    }
    std::memcpy(copy, text.constData(), static_cast<size_t>(text.size()));
    return copy;
}

int conversation(int numMsg, const pam_message **msg, pam_response **resp, void *appdataPtr)
{
    if (numMsg <= 0 || msg == nullptr || resp == nullptr) {
        return PAM_CONV_ERR;
    }

    auto *data = static_cast<PamConversationData *>(appdataPtr);
    auto *responses = static_cast<pam_response *>(std::calloc(static_cast<size_t>(numMsg), sizeof(pam_response)));
    if (responses == nullptr) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < numMsg; ++i) {
        const pam_message *message = msg[i];
        if (message == nullptr) {
            continue;
        }

        switch (message->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            responses[i].resp = dupCString(data != nullptr ? data->password : QByteArray());
            if (responses[i].resp == nullptr) {
                for (int j = 0; j < i; ++j) {
                    std::free(responses[j].resp);
                }
                std::free(responses);
                return PAM_BUF_ERR;
            }
            break;
        case PAM_TEXT_INFO:
        case PAM_ERROR_MSG:
            break;
        default:
            std::free(responses);
            return PAM_CONV_ERR;
        }
    }

    *resp = responses;
    return PAM_SUCCESS;
}

QString defaultUserName()
{
    const QByteArray user = qgetenv("USER");
    if (!user.isEmpty()) {
        return QString::fromLocal8Bit(user);
    }
    const QByteArray logname = qgetenv("LOGNAME");
    if (!logname.isEmpty()) {
        return QString::fromLocal8Bit(logname);
    }
    return {};
}

} // namespace

PamAuthenticator::PamAuthenticator(QObject *parent)
    : QObject(parent)
    , m_user(defaultUserName())
{
}

void PamAuthenticator::setService(const QString &service)
{
    if (!service.trimmed().isEmpty()) {
        m_service = service.trimmed();
    }
}

void PamAuthenticator::setUser(const QString &user)
{
    if (!user.trimmed().isEmpty()) {
        m_user = user.trimmed();
    }
}

void PamAuthenticator::authenticate(const QString &password)
{
    authenticateWithService(password, m_service);
}

void PamAuthenticator::authenticateWithService(const QString &password, const QString &serviceOverride)
{
    const QString requestedService = serviceOverride.trimmed().isEmpty() ? m_service : serviceOverride.trimmed();
    const int attemptId = ++m_nextAttemptId;
    m_latestAttemptId = attemptId;
    ++m_activeAttempts;
    setBusy(true);
    emit promptChanged(QStringLiteral("Authenticating"));

    const QString service = requestedService;
    const QString user = m_user;
    const QByteArray passwordBytes = password.toUtf8();

    QThread *worker = QThread::create([this, attemptId, service, user, passwordBytes]() {
        PamConversationData data { passwordBytes };
        pam_conv conv { conversation, &data };
        pam_handle_t *handle = nullptr;

        int result = pam_start(service.toLocal8Bit().constData(),
                               user.isEmpty() ? nullptr : user.toLocal8Bit().constData(),
                               &conv,
                               &handle);
        if (result == PAM_SUCCESS) {
            result = pam_authenticate(handle, 0);
        }
        if (result == PAM_SUCCESS) {
            result = pam_acct_mgmt(handle, 0);
        }

        const QString reason = handle != nullptr
            ? QString::fromLocal8Bit(pam_strerror(handle, result))
            : QStringLiteral("PAM failed to start");
        if (handle != nullptr) {
            pam_end(handle, result);
        }

        QMetaObject::invokeMethod(this, [this, attemptId, result, reason]() {
            m_activeAttempts = std::max(0, m_activeAttempts - 1);
            setBusy(m_activeAttempts > 0);
            if (result == PAM_SUCCESS) {
                emit messageChanged(QStringLiteral("Unlocked"));
                emit authenticationSucceeded();
            } else if (attemptId == m_latestAttemptId) {
                emit promptChanged(QStringLiteral("Try again"));
                emit authenticationFailed(reason);
            }
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void PamAuthenticator::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}
