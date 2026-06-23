#include "qbaripc.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QMetaObject>
#include <QStandardPaths>

QbarIpc *QbarIpc::instance()
{
    static QbarIpc *self = new QbarIpc(qApp);
    return self;
}

QbarIpc::QbarIpc(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &QbarIpc::onNewConnection);
}

QString QbarIpc::socketPath() const
{
    // $QBAR_IPC_SOCKET overrides; otherwise a fixed, predictable path under the per-user
    // runtime dir ($XDG_RUNTIME_DIR, i.e. /run/user/<uid>) so a keybind client always
    // knows where to connect. QLocalServer treats an absolute path as the socket path.
    const QByteArray env = qgetenv("QBAR_IPC_SOCKET");
    if (!env.isEmpty()) {
        return QString::fromLocal8Bit(env);
    }
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath();
    }
    return runtime + QStringLiteral("/qbar.sock");
}

void QbarIpc::start()
{
    if (m_server.isListening()) {
        return;
    }
    const QString path = socketPath();
    // Clear a socket left behind by a crashed prior instance, then listen.
    QLocalServer::removeServer(path);
    if (!m_server.listen(path)) {
        qWarning("QBar IPC: failed to listen on '%s': %s",
                 qPrintable(path), qPrintable(m_server.errorString()));
        return;
    }
    qInfo("QBar IPC listening on %s", qPrintable(m_server.fullServerName()));
}

void QbarIpc::registerPopup(const QString &name, QObject *target)
{
    if (name.isEmpty() || target == nullptr) {
        return;
    }
    m_popups.insert(name, QPointer<QObject>(target));
}

void QbarIpc::registerCommand(const QString &name, const QJSValue &callback)
{
    if (name.isEmpty() || !callback.isCallable()) {
        return;
    }
    m_commands.insert(name, callback); // overwrite on re-register (widget hot-reload)
}

void QbarIpc::registerBar(QObject *bar)
{
    if (bar != nullptr) {
        m_bars.append(QPointer<QObject>(bar));
    }
}

void QbarIpc::onNewConnection()
{
    while (QLocalSocket *sock = m_server.nextPendingConnection()) {
        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
            while (sock->canReadLine()) {
                sock->write(handleLine(sock->readLine()));
                sock->write("\n");
                sock->flush();
            }
        });
        connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
    }
}

QByteArray QbarIpc::handleLine(const QByteArray &line)
{
    const auto reply = [](bool ok, const QString &error = QString(),
                          QJsonObject extra = QJsonObject()) {
        extra.insert(QStringLiteral("ok"), ok);
        if (!error.isEmpty()) {
            extra.insert(QStringLiteral("error"), error);
        }
        return QJsonDocument(extra).toJson(QJsonDocument::Compact);
    };

    QJsonParseError perr {};
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        return reply(false, QStringLiteral("invalid JSON"));
    }

    const QJsonObject req = doc.object();
    const QString cmd = req.value(QStringLiteral("command")).toString();

    if (cmd == QLatin1String("ping")) {
        return reply(true, {}, {{QStringLiteral("pong"), true}});
    }
    if (cmd == QLatin1String("list")) {
        QJsonArray names;
        for (auto it = m_popups.constBegin(); it != m_popups.constEnd(); ++it) {
            if (it.value()) {
                names.append(it.key());
            }
        }
        return reply(true, {}, {{QStringLiteral("popups"), names}});
    }
    if (cmd == QLatin1String("commands")) {
        QJsonArray names;
        for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
            names.append(it.key());
        }
        return reply(true, {}, {{QStringLiteral("commands"), names}});
    }
    if (cmd == QLatin1String("trigger")) {
        const QString name = req.value(QStringLiteral("name")).toString();
        const auto it = m_commands.constFind(name);
        if (it == m_commands.constEnd() || !it.value().isCallable()) {
            return reply(false, QStringLiteral("unknown command: %1").arg(name));
        }
        const QJSValue result = it.value().call();
        if (result.isError()) {
            return reply(false, QStringLiteral("command '%1' threw: %2")
                                    .arg(name, result.toString()));
        }
        return reply(true);
    }
    if (cmd == QLatin1String("close-all")) {
        for (auto it = m_popups.constBegin(); it != m_popups.constEnd(); ++it) {
            if (it.value()) {
                QMetaObject::invokeMethod(it.value(), "close", Qt::DirectConnection);
            }
        }
        return reply(true);
    }
    if (cmd == QLatin1String("set-css")) {
        const QString path = req.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            return reply(false, QStringLiteral("set-css needs a \"path\""));
        }
        int applied = 0;
        for (const QPointer<QObject> &bar : m_bars) {
            bool ok = false;
            if (bar
                && QMetaObject::invokeMethod(bar, "setStyleSheet", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, ok), Q_ARG(QString, path))
                && ok) {
                ++applied;
            }
        }
        if (applied == 0) {
            return reply(false, QStringLiteral("could not apply css (missing file or no bars)"));
        }
        return reply(true, {}, {{QStringLiteral("bars"), applied}});
    }
    if (cmd == QLatin1String("reset-css")) {
        int applied = 0;
        for (const QPointer<QObject> &bar : m_bars) {
            bool ok = false;
            if (bar
                && QMetaObject::invokeMethod(bar, "resetStyleSheet", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, ok))
                && ok) {
                ++applied;
            }
        }
        if (applied == 0) {
            return reply(false, QStringLiteral("could not reset css (no bars)"));
        }
        return reply(true, {}, {{QStringLiteral("bars"), applied}});
    }
    if (cmd == QLatin1String("open") || cmd == QLatin1String("toggle")
        || cmd == QLatin1String("close")) {
        // "popup" and "drawer" are aliases — both popups and drawer groups register into
        // the same name→target registry and respond to open/toggle/close.
        QString name = req.value(QStringLiteral("popup")).toString();
        if (name.isEmpty()) {
            name = req.value(QStringLiteral("drawer")).toString();
        }
        const auto it = m_popups.constFind(name);
        if (it == m_popups.constEnd() || !it.value()) {
            return reply(false, QStringLiteral("unknown popup/drawer: %1").arg(name));
        }
        const char *method = cmd == QLatin1String("open") ? "open"
            : cmd == QLatin1String("close")               ? "close"
                                                          : "toggle";
        if (!QMetaObject::invokeMethod(it.value(), method, Qt::DirectConnection)) {
            return reply(false, QStringLiteral("could not invoke %1 on %2").arg(cmd, name));
        }
        return reply(true);
    }

    return reply(false, QStringLiteral("unknown command: %1").arg(cmd));
}
