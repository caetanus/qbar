#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

// Polls the keyboard Caps Lock LED via /sys/class/leds/*capslock*/brightness.
// Compositor-agnostic (works on Wayland where the bar surface never holds
// keyboard focus and so cannot read modifier state directly).
class CapsLockMonitor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit CapsLockMonitor(QObject *parent = nullptr);

    bool active() const { return m_active; }

signals:
    void activeChanged();

private:
    void poll();

    bool m_active = false;
    QStringList m_brightnessPaths;
    QTimer m_timer;
};
