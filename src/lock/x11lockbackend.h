#pragma once

#include "lockbackend.h"

#include <QAbstractNativeEventFilter>
#include <QList>

class X11LockBackend final : public LockBackend, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit X11LockBackend(QObject *parent = nullptr);
    ~X11LockBackend() override;

    QString name() const override { return QStringLiteral("x11"); }
    bool isAvailable() const override;
    QString unavailableReason() const override;
    void setGrabWindow(quintptr windowId);
    void setLockWindows(const QList<quintptr> &windowIds);

    // Re-raises the lock whenever any other window restacks: on X11 the lock
    // windows are override-redirect (the WM's stacking, and StaysOnTop, never
    // win against override-redirect siblings such as qbar's dock), so staying
    // on top is our own job — the i3lock approach.
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

public slots:
    void lock() override;
    void unlock() override;

private:
    bool grab();
    void ungrab();
    void startKeepAbove();
    void stopKeepAbove();
    void raiseLockWindows();

    void *m_connection = nullptr;
    quint32 m_cursor = 0;
    quintptr m_grabWindow = 0;
    QList<quintptr> m_lockWindows;
    quint32 m_rootPrevEventMask = 0;
    bool m_grabbed = false;
    bool m_keepingAbove = false;
};
