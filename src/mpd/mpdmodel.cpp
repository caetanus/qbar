#include "mpdmodel.h"

#include <QHostAddress>
#include <QTcpSocket>

namespace {

QString mpdHost()
{
    const QByteArray env = qgetenv("MPD_HOST");
    if (env.isEmpty()) {
        return QStringLiteral("localhost");
    }
    // MPD_HOST may carry "password@host"; qbar doesn't do passwords (a bar has no
    // business holding one) — use the host part.
    const qsizetype at = env.lastIndexOf('@');
    return QString::fromUtf8(at >= 0 ? env.mid(at + 1) : env);
}

quint16 mpdPort()
{
    const int port = qEnvironmentVariableIntValue("MPD_PORT");
    return port > 0 ? static_cast<quint16>(port) : 6600;
}

} // namespace

MpdModel::MpdModel(QObject *parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &MpdModel::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &MpdModel::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onDisconnected();
    });

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &MpdModel::connectToMpd);

    connectToMpd();
}

void MpdModel::connectToMpd()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    m_greeted = false;
    m_pendingCommand.clear();
    m_queue.clear();
    m_fields.clear();
    m_socket->connectToHost(mpdHost(), mpdPort());
}

void MpdModel::onDisconnected()
{
    m_socket->abort();
    if (m_connected) {
        m_connected = false;
        rebuild();
    }
    // Quiet backoff: MPD not running is a normal state for most machines.
    m_reconnect.start(m_reconnect.interval() == 0 ? 3000 : 30000);
}

void MpdModel::sendCommand(const QByteArray &command)
{
    if (m_pendingCommand.isEmpty()) {
        m_pendingCommand = command;
        m_socket->write(command + '\n');
        return;
    }
    if (m_pendingCommand == "idle" && command != "noidle") {
        // Wake the connection; the queued command runs when idle's reply lands.
        m_queue.append(command);
        m_socket->write("noidle\n");
        return;
    }
    m_queue.append(command);
}

void MpdModel::enterIdle()
{
    if (m_pendingCommand.isEmpty() && m_queue.isEmpty()) {
        sendCommand("idle player mixer options");
    }
}

void MpdModel::requestStatus()
{
    sendCommand("status");
    sendCommand("currentsong");
}

void MpdModel::onReadyRead()
{
    while (m_socket->canReadLine()) {
        QByteArray line = m_socket->readLine().trimmed();
        if (!m_greeted) {
            if (line.startsWith("OK MPD")) {
                m_greeted = true;
                m_connected = true;
                requestStatus();
            }
            continue;
        }
        if (line == "OK" || line.startsWith("ACK ")) {
            finishResponse();
            continue;
        }
        handleResponseLine(line);
    }
}

void MpdModel::handleResponseLine(const QByteArray &line)
{
    const qsizetype colon = line.indexOf(": ");
    if (colon <= 0) {
        return;
    }
    m_fields.insert(line.left(colon), line.mid(colon + 2));
}

void MpdModel::finishResponse()
{
    const QByteArray command = std::exchange(m_pendingCommand, {});
    const QHash<QByteArray, QByteArray> fields = std::exchange(m_fields, {});

    if (command == "status") {
        m_state = QString::fromUtf8(fields.value("state", "stop"));
        m_volume = fields.contains("volume") ? fields.value("volume").toInt() : -1;
        rebuild();
    } else if (command == "currentsong") {
        m_artist = QString::fromUtf8(fields.value("Artist"));
        m_title = QString::fromUtf8(fields.value("Title", fields.value("file")));
        m_album = QString::fromUtf8(fields.value("Album"));
        rebuild();
    } else if (command == "idle") {
        // Something changed (or noidle woke us) — refresh unless a command is queued
        // that will do it anyway.
        if (m_queue.isEmpty()) {
            requestStatus();
        }
    }

    if (!m_queue.isEmpty()) {
        const QByteArray next = m_queue.takeFirst();
        m_pendingCommand = next;
        m_socket->write(next + '\n');
        // Control commands have no data reply; a status refresh follows via idle.
        if (next == "pause" || next == "play" || next == "next" || next == "previous") {
            m_queue.append("status");
            m_queue.append("currentsong");
        }
        return;
    }
    enterIdle();
}

void MpdModel::rebuild()
{
    QString text;
    if (m_connected && m_state != QLatin1String("stop")) {
        text = m_artist.isEmpty() ? m_title : QStringLiteral("%1 — %2").arg(m_artist, m_title);
        if (m_state == QLatin1String("pause")) {
            text.prepend(QStringLiteral("⏸ "));
        }
    }
    m_displayText = text;
    emit changed();
}

void MpdModel::toggle()
{
    if (!m_connected) {
        return;
    }
    sendCommand(m_state == QLatin1String("play") ? QByteArrayLiteral("pause")
                                                 : QByteArrayLiteral("play"));
}

void MpdModel::next()
{
    if (m_connected) {
        sendCommand("next");
    }
}

void MpdModel::previous()
{
    if (m_connected) {
        sendCommand("previous");
    }
}
