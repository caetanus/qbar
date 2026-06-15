#include "wmbackendfactory.h"

#include "bspwmbackend.h"
#include "ewmhbackend.h"
#include "hyprlandbackend.h"
#include "nullbackend.h"
#include "../ipc/i3ipcclient.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace {

bool i3SwayAvailable()
{
    const auto environment = QProcessEnvironment::systemEnvironment();
    const QString swaySocket = environment.value(QStringLiteral("SWAYSOCK"));
    if (!swaySocket.isEmpty() && QFileInfo::exists(swaySocket)) {
        return true;
    }

    const QString i3Socket = environment.value(QStringLiteral("I3SOCK"));
    return !i3Socket.isEmpty() && QFileInfo::exists(i3Socket);
}

} // namespace

WindowManagerBackend *createWindowManagerBackend(const QString &backendName, QObject *parent)
{
    const QString requested = backendName.trimmed().toLower();
    WindowManagerBackend *backend = nullptr;

    if (requested == QStringLiteral("i3") || requested == QStringLiteral("sway")) {
        backend = new I3IpcClient(parent);
    } else if (requested == QStringLiteral("hyprland")) {
        backend = new HyprlandBackend(parent);
    } else if (requested == QStringLiteral("bspwm")) {
        backend = new BspwmBackend(parent);
    } else if (requested == QStringLiteral("ewmh") || requested == QStringLiteral("xworkspaces")) {
        backend = new EwmhBackend(parent);
    } else if (requested == QStringLiteral("none") || requested == QStringLiteral("null")) {
        backend = new NullBackend(parent);
    } else {
        if (i3SwayAvailable()) {
            backend = new I3IpcClient(parent);
        } else if (HyprlandBackend::isAvailable()) {
            backend = new HyprlandBackend(parent);
        } else if (BspwmBackend::isAvailable()) {
            backend = new BspwmBackend(parent);
        } else if (EwmhBackend::isAvailable()) {
            backend = new EwmhBackend(parent);
        } else {
            backend = new NullBackend(parent);
        }
    }

    qWarning() << "QBar window manager backend:" << backend->name();
    return backend;
}
