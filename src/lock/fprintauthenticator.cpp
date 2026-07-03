#include "fprintauthenticator.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>

namespace {

constexpr auto kService = "net.reactivated.Fprint";
constexpr auto kManagerPath = "/net/reactivated/Fprint/Manager";
constexpr auto kManagerIface = "net.reactivated.Fprint.Manager";
constexpr auto kDeviceIface = "net.reactivated.Fprint.Device";

QDBusMessage deviceCall(const QString &path, const QString &method)
{
    return QDBusMessage::createMethodCall(QString::fromLatin1(kService), path,
                                          QString::fromLatin1(kDeviceIface), method);
}

} // namespace

FprintAuthenticator::FprintAuthenticator(QObject *parent)
    : QObject(parent)
{
}

FprintAuthenticator::~FprintAuthenticator()
{
    stop();
}

bool FprintAuthenticator::isAvailable()
{
    if (!QDBusConnection::systemBus().isConnected()) {
        return false;
    }
    auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kManagerPath),
                                              QString::fromLatin1(kManagerIface),
                                              QStringLiteral("GetDefaultDevice"));
    const QDBusReply<QDBusObjectPath> reply = QDBusConnection::systemBus().call(msg);
    return reply.isValid() && !reply.value().path().isEmpty();
}

bool FprintAuthenticator::resolveDevice()
{
    if (!m_devicePath.isEmpty()) {
        return true;
    }
    auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kManagerPath),
                                              QString::fromLatin1(kManagerIface),
                                              QStringLiteral("GetDefaultDevice"));
    const QDBusReply<QDBusObjectPath> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid() || reply.value().path().isEmpty()) {
        return false;
    }
    m_devicePath = reply.value().path();
    return true;
}

void FprintAuthenticator::start()
{
    if (m_running) {
        return;
    }
    if (!resolveDevice()) {
        emit unavailable(tr("No fingerprint device"));
        return;
    }

    if (!m_signalsConnected) {
        m_signalsConnected = QDBusConnection::systemBus().connect(
            QString::fromLatin1(kService), m_devicePath, QString::fromLatin1(kDeviceIface),
            QStringLiteral("VerifyStatus"), this, SLOT(handleVerifyStatus(QString, bool)));
    }

    // Claim the device for this user (empty = the caller's uid, per fprintd).
    const QDBusReply<void> claim = QDBusConnection::systemBus().call(
        deviceCall(m_devicePath, QStringLiteral("Claim"))
            << (m_user.isEmpty() ? QString() : m_user));
    if (!claim.isValid()) {
        emit unavailable(tr("Fingerprint device busy: %1").arg(claim.error().message()));
        return;
    }
    m_claimed = true;
    m_running = true;
    beginVerify();
}

void FprintAuthenticator::beginVerify()
{
    if (!m_claimed || m_verifying) {
        return;
    }
    const QDBusReply<void> reply = QDBusConnection::systemBus().call(
        deviceCall(m_devicePath, QStringLiteral("VerifyStart")) << QStringLiteral("any"));
    if (reply.isValid()) {
        m_verifying = true;
        setActive(true);
        emit statusChanged(tr("Touch the fingerprint reader"));
    }
}

void FprintAuthenticator::endVerify()
{
    if (m_verifying) {
        QDBusConnection::systemBus().call(deviceCall(m_devicePath, QStringLiteral("VerifyStop")));
        m_verifying = false;
    }
    setActive(false);
}

void FprintAuthenticator::stop()
{
    m_running = false;
    if (m_devicePath.isEmpty()) {
        return;
    }
    endVerify();
    if (m_claimed) {
        QDBusConnection::systemBus().call(deviceCall(m_devicePath, QStringLiteral("Release")));
        m_claimed = false;
    }
    if (m_signalsConnected) {
        QDBusConnection::systemBus().disconnect(
            QString::fromLatin1(kService), m_devicePath, QString::fromLatin1(kDeviceIface),
            QStringLiteral("VerifyStatus"), this, SLOT(handleVerifyStatus(QString, bool)));
        m_signalsConnected = false;
    }
}

void FprintAuthenticator::handleVerifyStatus(const QString &result, bool done)
{
    if (!m_running) {
        return;
    }

    if (result == QLatin1String("verify-match")) {
        endVerify();
        emit authenticationSucceeded();
        return;
    }

    if (!done) {
        // Transient feedback (retry-scan, swipe-too-short, finger-not-centered, ...).
        emit statusChanged(tr("Fingerprint not recognized, try again"));
        return;
    }

    // A terminal non-match / error: stop this round and re-arm so the reader keeps
    // listening for another finger, unless we've been told to stop.
    endVerify();
    if (result == QLatin1String("verify-no-match")) {
        emit scanFailed(tr("Fingerprint did not match"));
    } else if (result == QLatin1String("verify-disconnected")) {
        emit unavailable(tr("Fingerprint reader disconnected"));
        m_running = false;
        return;
    }
    if (m_running) {
        beginVerify();
    }
}

void FprintAuthenticator::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    emit activeChanged(m_active);
}
