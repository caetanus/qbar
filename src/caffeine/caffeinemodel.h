#pragma once

#include <QObject>
#include <QString>
#include <QDBusUnixFileDescriptor>

class QWindow;
class QTimer;

class CaffeineModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit CaffeineModel(QWindow *window, QObject *parent = nullptr);
    ~CaffeineModel() override;

    bool active() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setActive(bool active);

signals:
    void activeChanged();

private:
    enum class Backend {
        None,
        WaylandIdleInhibit,
        Login1,
        ScreenSaver,
    };

    bool inhibitWayland();
    bool inhibit(bool enabled);
    bool inhibitLogin1();
    bool inhibitScreenSaver(const QString &service, const QString &path, const QString &interfaceName);
    void release();
    void syncState();
    bool backendActive() const;

    QWindow *m_window = nullptr;
    QTimer *m_syncTimer = nullptr;
    bool m_requestedActive = false;
    bool m_active = false;
    Backend m_backend = Backend::None;
    struct zwp_idle_inhibit_manager_v1 *m_idleInhibitManager = nullptr;
    struct zwp_idle_inhibitor_v1 *m_idleInhibitor = nullptr;
    QDBusUnixFileDescriptor m_login1Fd;
    uint m_cookie = 0;
};
