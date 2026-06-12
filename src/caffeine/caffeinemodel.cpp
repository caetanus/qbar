#include "caffeinemodel.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>

namespace {

constexpr auto login1Service = "org.freedesktop.login1";
constexpr auto login1Path = "/org/freedesktop/login1";
constexpr auto login1Interface = "org.freedesktop.login1.Manager";

constexpr auto screenSaverPath = "/ScreenSaver";
constexpr auto screenSaverInterface = "org.freedesktop.ScreenSaver";

} // namespace

CaffeineModel::CaffeineModel(QObject *parent)
    : QObject(parent)
{
}

bool CaffeineModel::active() const
{
    return m_active;
}

void CaffeineModel::toggle()
{
    setActive(!m_active);
}

void CaffeineModel::setActive(bool active)
{
    if (m_active == active) {
        return;
    }

    if (!inhibit(active)) {
        qWarning() << "[caffeine] toggle failed" << active;
        return;
    }

    m_active = active;
    emit activeChanged();
}

bool CaffeineModel::inhibit(bool enabled)
{
    if (enabled) {
        if (inhibitLogin1()) {
            return true;
        }

        if (inhibitScreenSaver(QStringLiteral("org.gnome.ScreenSaver"),
                               QStringLiteral("/org/gnome/ScreenSaver"),
                               QStringLiteral("org.gnome.ScreenSaver"))) {
            return true;
        }

        if (inhibitScreenSaver(QStringLiteral("org.freedesktop.ScreenSaver"),
                               QStringLiteral("/ScreenSaver"),
                               QStringLiteral("org.freedesktop.ScreenSaver"))) {
            return true;
        }

        return false;
    }

    release();
    return true;
}

bool CaffeineModel::inhibitLogin1()
{
    QDBusInterface iface(QString::fromLatin1(login1Service),
                         QString::fromLatin1(login1Path),
                         QString::fromLatin1(login1Interface),
                         QDBusConnection::systemBus());
    if (!iface.isValid()) {
        qWarning() << "[caffeine] login1 unavailable";
        return false;
    }

    const QDBusReply<QDBusUnixFileDescriptor> reply = iface.call(QStringLiteral("Inhibit"),
                                                                  QStringLiteral("idle"),
                                                                  QStringLiteral("qbar"),
                                                                  QStringLiteral("disable screen lock"),
                                                                  QStringLiteral("block"));
    if (!reply.isValid()) {
        qWarning() << "[caffeine] login1 Inhibit failed" << reply.error().message();
        return false;
    }

    release();
    m_login1Fd = reply.value();
    m_backend = Backend::Login1;
    qWarning() << "[caffeine] inhibited via login1";
    return true;
}

bool CaffeineModel::inhibitScreenSaver(const QString &service, const QString &path, const QString &interfaceName)
{
    QDBusInterface iface(service, path, interfaceName, QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        qWarning() << "[caffeine] screen saver interface unavailable" << service;
        return false;
    }

    const QDBusReply<uint> reply = iface.call(QStringLiteral("Inhibit"),
                                              QStringLiteral("qbar"),
                                              QStringLiteral("disable screen lock"));
    if (!reply.isValid()) {
        qWarning() << "[caffeine] Inhibit failed" << service << reply.error().message();
        return false;
    }

    release();
    m_cookie = reply.value();
    m_backend = Backend::ScreenSaver;
    qWarning() << "[caffeine] inhibited via" << service << m_cookie;
    return true;
}

void CaffeineModel::release()
{
    if (m_backend == Backend::Login1) {
        m_login1Fd = QDBusUnixFileDescriptor();
    } else if (m_backend == Backend::ScreenSaver && m_cookie != 0) {
        QDBusInterface gnomeIface(QStringLiteral("org.gnome.ScreenSaver"),
                                  QStringLiteral("/org/gnome/ScreenSaver"),
                                  QStringLiteral("org.gnome.ScreenSaver"),
                                  QDBusConnection::sessionBus());
        if (gnomeIface.isValid()) {
            const QDBusMessage reply = gnomeIface.call(QStringLiteral("UnInhibit"), m_cookie);
            if (reply.type() == QDBusMessage::ErrorMessage) {
                qWarning() << "[caffeine] UnInhibit failed" << reply.errorMessage();
            } else {
                qWarning() << "[caffeine] uninhibited" << m_cookie;
            }
        } else {
            QDBusInterface freedesktopIface(QStringLiteral("org.freedesktop.ScreenSaver"),
                                            QStringLiteral("/ScreenSaver"),
                                            QStringLiteral("org.freedesktop.ScreenSaver"),
                                            QDBusConnection::sessionBus());
            if (freedesktopIface.isValid()) {
                const QDBusMessage reply = freedesktopIface.call(QStringLiteral("UnInhibit"), m_cookie);
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    qWarning() << "[caffeine] UnInhibit failed" << reply.errorMessage();
                } else {
                    qWarning() << "[caffeine] uninhibited" << m_cookie;
                }
            }
        }
        m_cookie = 0;
    }

    m_backend = Backend::None;
}
