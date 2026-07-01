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
    Q_PROPERTY(bool numLockActive READ numLockActive NOTIFY numLockActiveChanged)

public:
    explicit CapsLockMonitor(QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool numLockActive() const { return m_numLockActive; }

signals:
    void activeChanged();
    void numLockActiveChanged();

private:
    void poll();
    static QStringList ledBrightnessPaths(const QString &pattern);
    static bool anyLedOn(const QStringList &paths);

    bool m_active = false;
    bool m_numLockActive = false;
    QStringList m_brightnessPaths;
    QStringList m_numBrightnessPaths;
    QTimer m_timer;
};
