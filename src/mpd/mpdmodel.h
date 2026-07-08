#pragma once

#include <QAbstractSocket>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QTcpSocket;

// Native MPD client (waybar's "mpd") over the plain-text protocol. Connects to
// $MPD_HOST:$MPD_PORT (localhost:6600 by default), keeps an `idle` command
// pending so player/mixer changes push instantly (no polling), and re-resolves
// status + currentsong on every wake-up. Auto-reconnects with backoff while MPD
// is down; `connected` is false then and the applet hides.
class MpdModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed) // play | pause | stop
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)

public:
    explicit MpdModel(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString state() const { return m_state; }
    QString artist() const { return m_artist; }
    QString title() const { return m_title; }
    QString album() const { return m_album; }
    QString displayText() const { return m_displayText; }
    int volume() const { return m_volume; }

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

private:
    void connectToMpd();
    void onReadyRead();
    void onDisconnected();
    void sendCommand(const QByteArray &command);
    void enterIdle();
    void requestStatus();
    void handleResponseLine(const QByteArray &line);
    void finishResponse();
    void rebuild();

    QTcpSocket *m_socket = nullptr;
    QTimer m_reconnect;
    QByteArray m_pendingCommand; // command whose reply is being read ("idle", "status", …)
    QList<QByteArray> m_queue;   // commands to run once the current reply finishes
    QHash<QByteArray, QByteArray> m_fields; // accumulated key: value pairs of the reply

    bool m_connected = false;
    bool m_greeted = false;
    QString m_state = QStringLiteral("stop");
    QString m_artist;
    QString m_title;
    QString m_album;
    QString m_displayText;
    int m_volume = -1;

signals:
    void changed();
};
