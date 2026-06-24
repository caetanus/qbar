#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// Reads the active X keyboard layout (the xkb group) straight from the X server.
//
// On sway the layout comes over the IPC `input` event, but i3/X11 doesn't report
// it there at all — so on i3 the layout indicator never updates. This polls the
// XKB state (active group) and the layout codes from the root `_XKB_RULES_NAMES`
// property, mirroring CapsLockMonitor's simple poll loop. X11-only: it's compiled
// (and instantiated) just for the i3/X11 backend.
struct xcb_connection_t;

class X11KeyboardLayout final : public QObject {
    Q_OBJECT

public:
    explicit X11KeyboardLayout(QObject *parent = nullptr);
    ~X11KeyboardLayout() override;

    QString layout() const { return m_layout; }

signals:
    void layoutChanged();

private:
    void poll();
    QStringList readLayoutCodes() const;

    xcb_connection_t *m_conn = nullptr;
    quint32 m_root = 0;
    quint32 m_rulesAtom = 0;
    bool m_xkbReady = false;
    QString m_layout;
    QTimer m_timer;
};
