#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// Lock-key state (waybar's "keyboard-state"): Caps/Num/Scroll Lock read from the
// kernel LED class (/sys/class/leds/*::capslock etc). The bar never has keyboard
// focus, so key events are not an option; the LED files are world-readable and a
// cheap sub-second poll (they are a couple of sysfs bytes) keeps it live on both
// X11 and Wayland with no compositor cooperation.
class KeyboardStateModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool capsLock READ capsLock NOTIFY changed)
    Q_PROPERTY(bool numLock READ numLock NOTIFY changed)
    Q_PROPERTY(bool scrollLock READ scrollLock NOTIFY changed)

public:
    explicit KeyboardStateModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool capsLock() const { return m_caps; }
    bool numLock() const { return m_num; }
    bool scrollLock() const { return m_scroll; }

signals:
    void changed();

private:
    void discoverLeds();
    void refresh();
    static bool readLed(const QStringList &paths);

    QStringList m_capsPaths;
    QStringList m_numPaths;
    QStringList m_scrollPaths;
    bool m_available = false;
    bool m_caps = false;
    bool m_num = false;
    bool m_scroll = false;
    QTimer m_timer;
};
