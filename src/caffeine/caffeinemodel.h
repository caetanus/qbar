#pragma once

#include <QObject>
#include <QString>
#include <QDBusUnixFileDescriptor>

class CaffeineModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit CaffeineModel(QObject *parent = nullptr);

    bool active() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setActive(bool active);

signals:
    void activeChanged();

private:
    enum class Backend {
        None,
        Login1,
        ScreenSaver,
    };

    bool inhibit(bool enabled);
    bool inhibitLogin1();
    bool inhibitScreenSaver(const QString &service, const QString &path, const QString &interfaceName);
    void release();

    bool m_active = false;
    Backend m_backend = Backend::None;
    QDBusUnixFileDescriptor m_login1Fd;
    uint m_cookie = 0;
};
