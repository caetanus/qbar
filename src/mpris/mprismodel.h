#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QDBusMessage;

// Tracks MPRIS2 media players on the session bus and exposes the active one
// (preferring a Playing player) with its metadata and transport controls.
class MprisModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(QString playerName READ playerName NOTIFY changed)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY changed)
    Q_PROPERTY(bool canPause READ canPause NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)

public:
    explicit MprisModel(QObject *parent = nullptr);

    bool available() const { return !m_activeService.isEmpty(); }
    bool playing() const { return m_status == QLatin1String("Playing"); }
    QString status() const { return m_status; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artUrl() const { return m_artUrl; }
    QString playerName() const { return m_playerName; }
    QString desktopEntry() const { return m_desktopEntry; }
    bool canPlay() const { return m_canPlay; }
    bool canPause() const { return m_canPause; }
    bool canGoNext() const { return m_canGoNext; }
    bool canGoPrevious() const { return m_canGoPrevious; }

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();

    // PID of the active player's bus connection, or -1. Used to focus the
    // player's window via the WM (windowless players yield a pid with no window).
    Q_INVOKABLE qint64 activePid() const;

signals:
    void changed();

private slots:
    void handleNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void handlePropertiesChanged();

private:
    void refreshPlayers();
    void trackPlayer(const QString &service);
    void untrackPlayer(const QString &service);
    void chooseActive();
    void refreshActiveState();
    void callPlayer(const QString &method);
    QVariant playerProperty(const QString &service, const QString &name) const;

    QStringList m_players;
    QString m_activeService;
    QString m_status;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artUrl;
    QString m_playerName;
    QString m_desktopEntry;
    bool m_canPlay = false;
    bool m_canPause = false;
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
};
