#include "gammabackend.h"

#include <QGuiApplication>

#include "waylandgamma.h"
#include "x11gamma.h"

GammaBackend *createGammaBackend(QObject *parent)
{
    const QString platform = QGuiApplication::platformName();
#ifdef QBAR_HAVE_WAYLAND
    if (platform.startsWith(QLatin1String("wayland"))) {
        auto *backend = new WaylandGammaBackend(parent);
        if (backend->init()) {
            return backend;
        }
        delete backend;
        return nullptr;
    }
#endif
#ifdef QBAR_HAVE_XCB_RANDR
    if (platform == QLatin1String("xcb")) {
        auto *backend = new X11GammaBackend(parent);
        if (backend->init()) {
            return backend;
        }
        delete backend;
        return nullptr;
    }
#endif
    Q_UNUSED(parent);
    return nullptr;
}
