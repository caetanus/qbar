#pragma once

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>

#include <QSize>

#include "ext-session-lock-v1-client-protocol.h"

// A QtWaylandClient shell integration that turns every qbar-lock QWindow into an
// ext-session-lock-v1 lock surface. Unlike layer-shell (a per-window role), a session
// lock is session-global: we bind the manager and acquire the lock ONCE at init, then
// hand out one lock surface per output. The lock is only genuinely secure once the
// compositor sends `locked`; if it sends `finished` instead the request was denied and
// we must fail closed (never present a bypassable overlay).
class QBarSessionLockIntegration final : public QtWaylandClient::QWaylandShellIntegration {
public:
    bool initialize(QtWaylandClient::QWaylandDisplay *display) override;
    QtWaylandClient::QWaylandShellSurface *createShellSurface(QtWaylandClient::QWaylandWindow *window) override;

    ext_session_lock_v1 *sessionLock() const { return m_lock; }

    static void handleLocked(void *data, ext_session_lock_v1 *lock);
    static void handleFinished(void *data, ext_session_lock_v1 *lock);

private:
    void publishLock();

    QtWaylandClient::QWaylandDisplay *m_display = nullptr;
    ext_session_lock_manager_v1 *m_lockManager = nullptr;
    ext_session_lock_v1 *m_lock = nullptr;
    bool m_locked = false;
    bool m_finished = false;
};

class QBarSessionLockSurface final : public QtWaylandClient::QWaylandShellSurface {
    Q_OBJECT

public:
    QBarSessionLockSurface(QBarSessionLockIntegration *integration, QtWaylandClient::QWaylandWindow *window);
    ~QBarSessionLockSurface() override;

    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect &rect) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // QWaylandShellSurface::setWindowSize(QSize) is a ~6.9 addition; older Qt
    // (Debian stable ships 6.8) only has the setWindowGeometry(QRect) virtual.
    void setWindowSize(const QSize &size) override;
#endif
    std::any surfaceRole() const override;
    // Suppress Qt's initial bufferless surface commit: ext-session-lock forbids committing
    // a null buffer, and (unlike xdg) the compositor sends the first configure without one.
    // The first real commit then carries a buffer, satisfying the protocol.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // The initial-bufferless-commit suppression hook only exists from ~6.9. On
    // older Qt the initial commit cannot be suppressed — the lock surface may
    // hit the null_buffer protocol error there (build works; runtime untested).
    bool commitSurfaceRole() const override { return false; }
#endif

    static void handleConfigure(void *data,
                                ext_session_lock_surface_v1 *surface,
                                uint32_t serial,
                                uint32_t width,
                                uint32_t height);

private:
    ext_session_lock_surface_v1 *m_lockSurface = nullptr;
    QSize m_pendingSize;
    QSize m_configuredSize;
    bool m_configured = false;
};

class QBarSessionLockPlugin final : public QtWaylandClient::QWaylandShellIntegrationPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.WaylandClient.QWaylandShellIntegrationFactoryInterface.5.3" FILE "qbar-session-lock.json")

public:
    QtWaylandClient::QWaylandShellIntegration *create(const QString &key, const QStringList &paramList) override;
};
