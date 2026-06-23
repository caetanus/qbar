#pragma once

#include "lockbackend.h"

class X11LockBackend final : public LockBackend {
    Q_OBJECT

public:
    explicit X11LockBackend(QObject *parent = nullptr);
    ~X11LockBackend() override;

    QString name() const override { return QStringLiteral("x11"); }
    bool isAvailable() const override;
    QString unavailableReason() const override;
    void setGrabWindow(quintptr windowId);

public slots:
    void lock() override;
    void unlock() override;

private:
    bool grab();
    void ungrab();

    void *m_connection = nullptr;
    quint32 m_cursor = 0;
    quintptr m_grabWindow = 0;
    bool m_grabbed = false;
};
