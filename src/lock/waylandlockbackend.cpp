#include "waylandlockbackend.h"

#ifdef QBAR_LOCK_HAVE_WAYLAND

#include <QCoreApplication>
#include <QGuiApplication>
#include <QVariant>
#include <qpa/qplatformnativeinterface.h>

#include <cstring>
#include <wayland-client.h>

#include "ext-session-lock-v1-client-protocol.h"

namespace {

wl_display *waylandDisplay()
{
    auto *native = QGuiApplication::platformNativeInterface();
    if (native == nullptr) {
        return nullptr;
    }
    return static_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("display")));
}

struct ProbeState {
    bool managerPresent = false;
};

void probeRegistryGlobal(void *data, wl_registry *, uint32_t, const char *interface, uint32_t)
{
    auto *state = static_cast<ProbeState *>(data);
    if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        state->managerPresent = true;
    }
}

void probeRegistryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener probeListener = {
    probeRegistryGlobal,
    probeRegistryGlobalRemove,
};

// The compositor must advertise ext_session_lock_manager_v1, or we cannot lock securely.
bool compositorSupportsSessionLock()
{
    wl_display *display = waylandDisplay();
    if (display == nullptr) {
        return false;
    }
    ProbeState state;
    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &probeListener, &state);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    return state.managerPresent;
}

ext_session_lock_v1 *publishedLock()
{
    if (QCoreApplication::instance() == nullptr) {
        return nullptr;
    }
    const QVariant handle = QCoreApplication::instance()->property("qbarSessionLock");
    if (!handle.isValid()) {
        return nullptr;
    }
    return reinterpret_cast<ext_session_lock_v1 *>(handle.value<quintptr>());
}

} // namespace

WaylandLockBackend::WaylandLockBackend(QObject *parent)
    : LockBackend(parent)
{
}

bool WaylandLockBackend::isAvailable() const
{
    return !qgetenv("WAYLAND_DISPLAY").isEmpty() && compositorSupportsSessionLock();
}

QString WaylandLockBackend::unavailableReason() const
{
    if (qgetenv("WAYLAND_DISPLAY").isEmpty()) {
        return QStringLiteral("Wayland display is not available");
    }
    return QStringLiteral("Compositor does not support ext-session-lock-v1");
}

void WaylandLockBackend::lock()
{
    // The qbar-session-lock shell integration plugin already acquired the lock at
    // platform init (before the first window). Here we only verify the compositor
    // actually supports it and fail closed otherwise — never present a fake lock.
    if (!compositorSupportsSessionLock()) {
        emit lockFailed(unavailableReason());
        return;
    }
    emit locked();
}

void WaylandLockBackend::unlock()
{
    // Deactivate the session lock cleanly BEFORE the process exits, so the compositor
    // reveals the session instead of holding a blank/solid-color lock. The lock handle
    // is published on the QGuiApplication by the plugin when the `locked` event arrives.
    if (ext_session_lock_v1 *lock = publishedLock()) {
        ext_session_lock_v1_unlock_and_destroy(lock);
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->setProperty("qbarSessionLock", QVariant());
        }
        if (wl_display *display = waylandDisplay()) {
            wl_display_flush(display);
        }
    }
    emit unlocked();
}

#else // QBAR_LOCK_HAVE_WAYLAND

WaylandLockBackend::WaylandLockBackend(QObject *parent)
    : LockBackend(parent)
{
}

bool WaylandLockBackend::isAvailable() const
{
    return false;
}

QString WaylandLockBackend::unavailableReason() const
{
    if (qgetenv("WAYLAND_DISPLAY").isEmpty()) {
        return QStringLiteral("Wayland display is not available");
    }
    return QStringLiteral("qbar-lock was built without Wayland (ext-session-lock-v1) support");
}

void WaylandLockBackend::lock()
{
    emit lockFailed(unavailableReason());
}

void WaylandLockBackend::unlock()
{
    emit unlocked();
}

#endif // QBAR_LOCK_HAVE_WAYLAND
