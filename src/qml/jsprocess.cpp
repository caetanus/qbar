#include "jsprocess.h"

#include <QJSEngine>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QTimer>

ProcReply::ProcReply(QProcess *proc, int timeoutMs, QObject *parent)
    : QObject(parent)
    , m_proc(proc)
{
    proc->setParent(this);

    connect(proc, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (m_settled) {
                    return;
                }
                const QString out = QString::fromUtf8(m_proc->readAllStandardOutput());
                const QString err = QString::fromUtf8(m_proc->readAllStandardError());
                if (status == QProcess::CrashExit) {
                    emit failed(QStringLiteral("process crashed"));
                } else {
                    emit finished(exitCode, out, err);
                }
                settle();
            });

    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // The only error that won't also surface via finished() is a failure to launch.
        if (!m_settled && error == QProcess::FailedToStart) {
            emit failed(m_proc->errorString());
            settle();
        }
    });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this]() {
            if (!m_settled && m_proc != nullptr && m_proc->state() != QProcess::NotRunning) {
                emit failed(QStringLiteral("timeout"));
                settle(); // settle() kills the process before tearing down
            }
        });
    }
}

void ProcReply::settle()
{
    if (m_settled) {
        return;
    }
    m_settled = true;
    if (m_proc != nullptr && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
    }
    deleteLater(); // one-shot: the JS handlers have run, free the wrapper (and its QProcess)
}

void ProcReply::write(const QString &data)
{
    if (m_proc != nullptr) {
        m_proc->write(data.toUtf8());
    }
}

void ProcReply::closeWrite()
{
    if (m_proc != nullptr) {
        m_proc->closeWriteChannel();
    }
}

void ProcReply::kill()
{
    if (m_proc != nullptr) {
        m_proc->kill();
    }
}

JsProcess::JsProcess(QObject *parent)
    : QObject(parent)
{
}

void JsProcess::install(QQmlEngine *engine)
{
    if (engine == nullptr) {
        return;
    }
    auto *proc = new JsProcess(engine);
    const QJSValue wrapper = engine->newQObject(proc);
    engine->globalObject().setProperty(QStringLiteral("Proc"), wrapper);
}

ProcReply *JsProcess::run(const QString &program, const QVariantList &args,
                          const QVariantMap &options)
{
    auto *proc = new QProcess;

    QStringList argList;
    argList.reserve(args.size());
    for (const QVariant &arg : args) {
        argList << arg.toString();
    }

    if (options.contains(QStringLiteral("cwd"))) {
        proc->setWorkingDirectory(options.value(QStringLiteral("cwd")).toString());
    }
    if (options.contains(QStringLiteral("env"))) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QVariantMap overrides = options.value(QStringLiteral("env")).toMap();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
            env.insert(it.key(), it.value().toString());
        }
        proc->setProcessEnvironment(env);
    }
    if (options.value(QStringLiteral("mergeStderr")).toBool()) {
        proc->setProcessChannelMode(QProcess::MergedChannels);
    }

    const int timeoutMs = options.value(QStringLiteral("timeout"), 0).toInt();
    auto *reply = new ProcReply(proc, timeoutMs, this);
    // Parent + explicit CppOwnership: the wrapper outlives the JS return and frees itself
    // via deleteLater() once the command settles, instead of being GC'd mid-flight.
    QQmlEngine::setObjectOwnership(reply, QQmlEngine::CppOwnership);

    proc->start(program, argList);

    // stdin: write it up front (QProcess buffers until started) and signal EOF so filter-style
    // commands proceed. With no input, leave stdin open — the caller can write()/closeWrite().
    if (options.contains(QStringLiteral("input"))) {
        proc->write(options.value(QStringLiteral("input")).toString().toUtf8());
        proc->closeWriteChannel();
    }

    return reply;
}
