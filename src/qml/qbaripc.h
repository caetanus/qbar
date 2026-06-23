#pragma once

#include <QHash>
#include <QJSValue>
#include <QLocalServer>
#include <QObject>
#include <QPointer>

// A small JSON line-protocol IPC over a QLocalSocket, so external tools (e.g. a
// keyboard-shortcut binding) can drive the bar — primarily to open/toggle popups.
//
// Socket name: `qbar` by default, overridable with $QBAR_IPC_SOCKET. One server per
// qbar process; every bar's popups register into the shared registry, so a single
// endpoint reaches popups across all bars.
//
// Protocol: one JSON object per line, reply is one JSON object per line.
//   {"command":"toggle","popup":"cpu"}   -> {"ok":true}
//   {"command":"open","popup":"memory"}  -> {"ok":true}
//   {"command":"close","popup":"clock"}  -> {"ok":true}
//   {"command":"close-all"}              -> {"ok":true}
//   {"command":"list"}                   -> {"ok":true,"popups":["cpu","memory",...]}
//   {"command":"trigger","name":"speedtest-run"} -> {"ok":true}
//   {"command":"commands"}               -> {"ok":true,"commands":["speedtest-run",...]}
//   {"command":"ping"}                   -> {"ok":true,"pong":true}
// Unknown popup/command/JSON -> {"ok":false,"error":"..."}.
//
// A registered target is any QObject exposing invokable open()/toggle()/close()
// (every QBar.Popup does; Clock registers a shim that re-emits its activated(); a
// drawer Group registers under its name and open/toggle/close pin it expanded). The
// open/toggle/close commands accept the name under either "popup" or "drawer".
//
// Beyond popups, a custom tool/widget can register an arbitrary named ACTION via
// registerCommand(name, jsCallback); the `trigger` command invokes it. This lets a
// widget expose, say, "speedtest-run" so a sway keybind can start a measurement:
//   bindsym $mod+s exec qbar-ipc trigger speedtest-run

class QbarIpc final : public QObject {
    Q_OBJECT

public:
    static QbarIpc *instance();

    // Begin listening (idempotent). Call once after the QApplication exists.
    void start();

    // Register a named popup target (called from QML). The target must expose
    // invokable open()/toggle()/close() methods.
    Q_INVOKABLE void registerPopup(const QString &name, QObject *target);

    // Register a named custom action (called from QML). `callback` is a JS function that
    // the IPC `trigger` command invokes — e.g. a widget registering "speedtest-run".
    // Re-registering the same name overwrites (survives widget hot-reload).
    Q_INVOKABLE void registerCommand(const QString &name, const QJSValue &callback);

    // Register a bar (called from C++). The bar must expose an invokable
    // setStyleSheet(QString) so the IPC `set-css` command can swap themes.
    void registerBar(QObject *bar);

private:
    explicit QbarIpc(QObject *parent = nullptr);

    void onNewConnection();

    QByteArray handleLine(const QByteArray &line);
    QString socketPath() const;

    QLocalServer m_server;
    QHash<QString, QPointer<QObject>> m_popups;
    QHash<QString, QJSValue> m_commands;
    QList<QPointer<QObject>> m_bars;
};
