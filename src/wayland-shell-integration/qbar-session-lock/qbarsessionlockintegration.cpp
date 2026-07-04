#include "qbarsessionlockintegration.h"

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>

#include <QCoreApplication>
#include <qpa/qwindowsysteminterface.h>
#include <QGuiApplication>
#include <QDebug>

#include <cstring>
#include <wayland-client.h>

namespace {

struct RegistryState {
    ext_session_lock_manager_v1 *lockManager = nullptr;
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t /*version*/)
{
    auto *state = static_cast<RegistryState *>(data);
    if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        state->lockManager = static_cast<ext_session_lock_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

const ext_session_lock_v1_listener sessionLockListener = {
    QBarSessionLockIntegration::handleLocked,
    QBarSessionLockIntegration::handleFinished,
};

const ext_session_lock_surface_v1_listener lockSurfaceListener = {
    QBarSessionLockSurface::handleConfigure,
};

} // namespace

bool QBarSessionLockIntegration::initialize(QtWaylandClient::QWaylandDisplay *display)
{
    qDebug() << "QBar session-lock integration initialize";
    m_display = display;

    RegistryState state;
    wl_display *wlDisplay = display->wl_display();
    wl_registry *registry = wl_display_get_registry(wlDisplay);
    wl_registry_add_listener(registry, &registryListener, &state);
    wl_display_roundtrip(wlDisplay);
    wl_registry_destroy(registry);

    m_lockManager = state.lockManager;
    if (m_lockManager == nullptr) {
        // No compositor support: refuse rather than fall back to an insecure toplevel.
        // WaylandLockBackend performs the same check and fails closed (exit 2).
        qWarning() << "QBar session-lock: ext_session_lock_manager_v1 not available";
        return false;
    }

    // Acquire the session lock now, once, for the whole session. The lock surfaces
    // (one per output) are created lazily as qbar-lock shows its QWindows.
    m_lock = ext_session_lock_manager_v1_lock(m_lockManager);
    ext_session_lock_v1_add_listener(m_lock, &sessionLockListener, this);
    wl_display_flush(wlDisplay);
    qWarning() << "QBar session-lock: lock requested";
    return true;
}

QtWaylandClient::QWaylandShellSurface *QBarSessionLockIntegration::createShellSurface(QtWaylandClient::QWaylandWindow *window)
{
    return new QBarSessionLockSurface(this, window);
}

void QBarSessionLockIntegration::publishLock()
{
    // Share the lock handle with qbar-lock's WaylandLockBackend via the shared
    // QGuiApplication instance so it can unlock_and_destroy on successful auth.
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->setProperty(
            "qbarSessionLock", QVariant::fromValue(reinterpret_cast<quintptr>(m_lock)));
        QCoreApplication::instance()->setProperty("qbarSessionLocked", m_locked);
    }
}

void QBarSessionLockIntegration::handleLocked(void *data, ext_session_lock_v1 * /*lock*/)
{
    auto *self = static_cast<QBarSessionLockIntegration *>(data);
    self->m_locked = true;
    qWarning() << "QBar session-lock: session locked";
    self->publishLock();
}

void QBarSessionLockIntegration::handleFinished(void *data, ext_session_lock_v1 * /*lock*/)
{
    auto *self = static_cast<QBarSessionLockIntegration *>(data);
    self->m_finished = true;
    // The lock could not be granted (e.g. another locker holds it) or was ended
    // abnormally. Fail closed: never leave a bypassable window on screen.
    qWarning() << "QBar session-lock: finished (lock denied/ended) — exiting";
    if (QCoreApplication::instance() != nullptr) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), []() { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    }
}

// --- QBarSessionLockSurface -------------------------------------------------

QBarSessionLockSurface::QBarSessionLockSurface(QBarSessionLockIntegration *integration,
                                               QtWaylandClient::QWaylandWindow *window)
    : QtWaylandClient::QWaylandShellSurface(window)
{
    ::wl_output *output = nullptr;
    if (window != nullptr) {
        if (auto *screen = window->waylandScreen()) {
            output = screen->output();
        }
    }

    m_lockSurface = ext_session_lock_v1_get_lock_surface(
        integration->sessionLock(), wlSurface(), output);
    ext_session_lock_surface_v1_add_listener(m_lockSurface, &lockSurfaceListener, this);
    qDebug() << "QBar session-lock surface created";
}

QBarSessionLockSurface::~QBarSessionLockSurface()
{
    if (m_lockSurface != nullptr) {
        ext_session_lock_surface_v1_destroy(m_lockSurface);
    }
}

bool QBarSessionLockSurface::isExposed() const
{
    return m_configured;
}

void QBarSessionLockSurface::applyConfigure()
{
    if (m_configuredSize.isValid()) {
        resizeFromApplyConfigure(m_configuredSize);
    }
}

void QBarSessionLockSurface::setWindowGeometry(const QRect &rect)
{
    Q_UNUSED(rect);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
void QBarSessionLockSurface::setWindowSize(const QSize &size)
{
    Q_UNUSED(size);
}
#endif

std::any QBarSessionLockSurface::surfaceRole() const
{
    return m_lockSurface;
}

void QBarSessionLockSurface::handleConfigure(void *data,
                                             ext_session_lock_surface_v1 *surface,
                                             uint32_t serial,
                                             uint32_t width,
                                             uint32_t height)
{
    auto *self = static_cast<QBarSessionLockSurface *>(data);
    // A lock surface MUST size itself to exactly what the compositor configured.
    ext_session_lock_surface_v1_ack_configure(surface, serial);
    self->m_configuredSize = QSize(width > 0 ? static_cast<int>(width) : 1,
                                   height > 0 ? static_cast<int>(height) : 1);
    self->m_configured = true;
    self->resizeFromApplyConfigure(self->m_configuredSize);

    // Defer the exposure kick out of this wl-event dispatch (same rationale as the
    // layer-shell integration: calling updateExposure() synchronously from a listener
    // re-enters Qt's window machinery mid-dispatch and races surface teardown).
    if (self->window() != nullptr) {
        auto *w = self->window();
        QMetaObject::invokeMethod(
            w, [w]() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                w->updateExposure();
#else
                QWindowSystemInterface::handleExposeEvent(
                    w->window(), QRect(QPoint(), w->geometry().size()));
#endif
                w->requestUpdate();
            }, Qt::QueuedConnection);
    }

    // Apply the pending configure so Qt renders + commits the first frame (with a buffer).
    // Without this the surface stays unrendered — black until an input event forces a
    // repaint — because commitSurfaceRole() suppressed Qt's own initial commit.
    self->applyConfigureWhenPossible();
}

// --- Plugin factory ---------------------------------------------------------

QtWaylandClient::QWaylandShellIntegration *QBarSessionLockPlugin::create(const QString &key,
                                                                         const QStringList &paramList)
{
    Q_UNUSED(key);
    Q_UNUSED(paramList);
    return new QBarSessionLockIntegration;
}
