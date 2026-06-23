#include "waylandlockbackend.h"

#include <QByteArray>

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
    return QStringLiteral("Wayland ext-session-lock-v1 backend is not implemented yet");
}

void WaylandLockBackend::lock()
{
    emit lockFailed(unavailableReason());
}

void WaylandLockBackend::unlock()
{
    emit unlocked();
}
