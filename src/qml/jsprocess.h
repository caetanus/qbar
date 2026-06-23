#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QProcess;
class QQmlEngine;

// A QProcess transport exposed to QML/JS as the global `Proc`, for custom widgets that need
// to run external commands (the QML side has no process API). Async and signal-based like
// `Http`/NetReply: it never blocks the GUI thread and frees itself once the command settles.
//
//   const p = Proc.run("sh", ["-c", "uptime"], { timeout: 5000 })
//   p.finished.connect((code, out, err) => { ... })
//   p.failed.connect(err => { ... })
//
// options: { input (stdin string), timeout (ms, 0 = none), cwd, env (map merged over the
// inherited environment), mergeStderr (bool) }. Methods: write(data), closeWrite(), kill().
// Arguments are passed as a list (no shell parsing) — use `sh -c` explicitly if you need a
// shell. Custom widgets are user-authored config, so this is as trusted as a waybar script.

class ProcReply final : public QObject {
    Q_OBJECT

public:
    explicit ProcReply(QProcess *proc, int timeoutMs, QObject *parent = nullptr);

    Q_INVOKABLE void write(const QString &data);
    Q_INVOKABLE void closeWrite();
    Q_INVOKABLE void kill();

signals:
    // Command exited: its exit code plus the captured stdout/stderr.
    void finished(int exitCode, const QString &stdoutText, const QString &stderrText);
    // Could not run / crashed / timed out / killed.
    void failed(const QString &error);

private:
    void settle(); // one-shot teardown after finished()/failed()

    QProcess *m_proc = nullptr;
    bool m_settled = false;
};

class JsProcess final : public QObject {
    Q_OBJECT

public:
    explicit JsProcess(QObject *parent = nullptr);

    // Install as the JS global `Proc` on the engine. Once per engine, before loading QML.
    static void install(QQmlEngine *engine);

    Q_INVOKABLE ProcReply *run(const QString &program, const QVariantList &args,
                               const QVariantMap &options);
};
