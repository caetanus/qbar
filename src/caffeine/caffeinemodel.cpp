#include "caffeinemodel.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QTimer>
#include <QWindow>
#ifdef QBAR_HAVE_WAYLAND
#include <qpa/qplatformnativeinterface.h>
#endif
#include <algorithm>
#include <cstring>

#ifdef QBAR_HAVE_WAYLAND
#include "idle-inhibit-unstable-v1-client-protocol.h"
#endif

namespace {

#ifdef QBAR_HAVE_WAYLAND

struct IdleInhibitRegistryState {
    zwp_idle_inhibit_manager_v1 **manager = nullptr;
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *state = static_cast<IdleInhibitRegistryState *>(data);
    if (state == nullptr || state->manager == nullptr) {
        return;
    }

    if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        *state->manager = static_cast<zwp_idle_inhibit_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, std::min(version, 1U)));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

wl_surface *surfaceFor(QWindow *window)
{
    auto *native = QGuiApplication::platformNativeInterface();
    if (native == nullptr || window == nullptr) {
        return nullptr;
    }

    return static_cast<wl_surface *>(native->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
}

bool ensureIdleInhibitManager(zwp_idle_inhibit_manager_v1 **manager)
{
    if (manager == nullptr || *manager != nullptr) {
        return manager != nullptr;
    }

    auto *native = QGuiApplication::platformNativeInterface();
    auto *display = native != nullptr
        ? static_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("display")))
        : nullptr;
    if (display == nullptr) {
        return false;
    }

    wl_registry *registry = wl_display_get_registry(display);
    if (registry == nullptr) {
        return false;
    }

    IdleInhibitRegistryState state;
    state.manager = manager;
    wl_registry_add_listener(registry, &registryListener, &state);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    return *manager != nullptr;
}

#endif

constexpr auto login1Service = "org.freedesktop.login1";
constexpr auto login1Path = "/org/freedesktop/login1";
constexpr auto login1Interface = "org.freedesktop.login1.Manager";

constexpr auto screenSaverPath = "/ScreenSaver";
constexpr auto screenSaverInterface = "org.freedesktop.ScreenSaver";

} // namespace

CaffeineModel::CaffeineModel(QWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_syncTimer(new QTimer(this))
{
    m_syncTimer->setInterval(2000);
    m_syncTimer->setSingleShot(false);
    connect(m_syncTimer, &QTimer::timeout, this, &CaffeineModel::syncState);
    m_syncTimer->start();
}

CaffeineModel::~CaffeineModel()
{
    release();
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
    if (m_requestedActive == active && m_active == active) {
        return;
    }

    m_requestedActive = active;
    syncState();
}

bool CaffeineModel::inhibit(bool enabled)
{
    if (enabled) {
        if (inhibitWayland()) {
            return true;
        }

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

bool CaffeineModel::inhibitWayland()
{
#ifdef QBAR_HAVE_WAYLAND
    if (m_window == nullptr) {
        return false;
    }

    if (!ensureIdleInhibitManager(&m_idleInhibitManager)) {
        qWarning() << "[caffeine] wayland idle-inhibit unavailable";
        return false;
    }

    if (m_idleInhibitor != nullptr) {
        return true;
    }

    auto *surface = surfaceFor(m_window);
    if (surface == nullptr) {
        qWarning() << "[caffeine] wayland surface unavailable";
        return false;
    }

    m_idleInhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(m_idleInhibitManager, surface);
    if (m_idleInhibitor == nullptr) {
        qWarning() << "[caffeine] wayland idle inhibitor creation failed";
        return false;
    }

    m_backend = Backend::WaylandIdleInhibit;
    qWarning() << "[caffeine] inhibited via wayland idle-inhibit";
    return true;
#else
    return false;
#endif
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
#ifdef QBAR_HAVE_WAYLAND
    if (m_backend == Backend::WaylandIdleInhibit && m_idleInhibitor != nullptr) {
        zwp_idle_inhibitor_v1_destroy(m_idleInhibitor);
        m_idleInhibitor = nullptr;
        qWarning() << "[caffeine] wayland inhibitor released";
    } else
#endif
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

bool CaffeineModel::backendActive() const
{
    switch (m_backend) {
    case Backend::WaylandIdleInhibit:
#ifdef QBAR_HAVE_WAYLAND
        return m_idleInhibitor != nullptr;
#else
        return false;
#endif
    case Backend::Login1:
        return m_login1Fd.isValid();
    case Backend::ScreenSaver:
        return m_cookie != 0;
    case Backend::None:
        return false;
    }

    return false;
}

void CaffeineModel::syncState()
{
    const bool backendNowActive = backendActive();

    if (m_requestedActive) {
        if (!backendNowActive) {
            if (!inhibit(true)) {
                qWarning() << "[caffeine] sync failed to enable backend";
                if (m_active) {
                    m_active = false;
                    emit activeChanged();
                }
                return;
            }
        }

        if (!m_active) {
            m_active = true;
            emit activeChanged();
        }
        return;
    }

    if (backendNowActive) {
        release();
    }

    if (m_active) {
        m_active = false;
        emit activeChanged();
    }
}
