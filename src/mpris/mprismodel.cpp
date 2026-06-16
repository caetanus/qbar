#include "mprismodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>

namespace {

const QString kServicePrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kRootIface = QStringLiteral("org.mpris.MediaPlayer2");
const QString kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

QVariant getProperty(const QString &service, const QString &iface, const QString &name)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, kObjectPath, kPropsIface, QStringLiteral("Get"));
    msg << iface << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().first().value<QDBusVariant>().variant();
}

QString variantToString(const QVariant &value)
{
    if (value.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        const QStringList list = qdbus_cast<QStringList>(value.value<QDBusArgument>());
        return list.join(QStringLiteral(", "));
    }
    if (value.canConvert<QStringList>() && value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList().join(QStringLiteral(", "));
    }
    return value.toString();
}

} // namespace

MprisModel::MprisModel(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("NameOwnerChanged"),
                this,
                SLOT(handleNameOwnerChanged(QString, QString, QString)));
    refreshPlayers();
}

void MprisModel::refreshPlayers()
{
    const QDBusReply<QStringList> reply = QDBusConnection::sessionBus().interface()->registeredServiceNames();
    if (!reply.isValid()) {
        return;
    }
    for (const QString &name : reply.value()) {
        if (name.startsWith(kServicePrefix)) {
            trackPlayer(name);
        }
    }
    chooseActive();
}

void MprisModel::trackPlayer(const QString &service)
{
    if (m_players.contains(service)) {
        return;
    }
    m_players.append(service);
    QDBusConnection::sessionBus().connect(service, kObjectPath, kPropsIface,
                                          QStringLiteral("PropertiesChanged"),
                                          this, SLOT(handlePropertiesChanged()));
}

void MprisModel::untrackPlayer(const QString &service)
{
    if (!m_players.removeOne(service)) {
        return;
    }
    QDBusConnection::sessionBus().disconnect(service, kObjectPath, kPropsIface,
                                             QStringLiteral("PropertiesChanged"),
                                             this, SLOT(handlePropertiesChanged()));
    if (service == m_activeService) {
        m_activeService.clear();
        chooseActive();
    }
}

void MprisModel::handleNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    if (!name.startsWith(kServicePrefix)) {
        return;
    }
    if (newOwner.isEmpty() && !oldOwner.isEmpty()) {
        untrackPlayer(name);
    } else if (oldOwner.isEmpty() && !newOwner.isEmpty()) {
        trackPlayer(name);
        chooseActive();
    }
}

void MprisModel::handlePropertiesChanged()
{
    // A player's status may have changed which one should be active.
    chooseActive();
    refreshActiveState();
}

void MprisModel::chooseActive()
{
    if (m_players.isEmpty()) {
        if (!m_activeService.isEmpty()) {
            m_activeService.clear();
            refreshActiveState();
        }
        return;
    }

    // Prefer a Playing player; otherwise a Paused one (sticking with the current
    // active player if it's still paused). A Stopped-only set leaves no active
    // player, so the bar applet hides instead of showing idle media.
    QString playingService;
    QString pausedService;
    for (const QString &service : m_players) {
        const QString status = getProperty(service, kPlayerIface, QStringLiteral("PlaybackStatus")).toString();
        if (status == QLatin1String("Playing")) {
            if (playingService.isEmpty()) {
                playingService = service;
            }
        } else if (status == QLatin1String("Paused")) {
            if (pausedService.isEmpty() || service == m_activeService) {
                pausedService = service;
            }
        }
    }

    m_activeService = !playingService.isEmpty() ? playingService : pausedService;
    refreshActiveState();
}

void MprisModel::refreshActiveState()
{
    if (m_activeService.isEmpty()) {
        m_status.clear();
        m_title.clear();
        m_artist.clear();
        m_album.clear();
        m_artUrl.clear();
        m_playerName.clear();
        m_desktopEntry.clear();
        m_canPlay = m_canPause = m_canGoNext = m_canGoPrevious = false;
        emit changed();
        return;
    }

    m_status = getProperty(m_activeService, kPlayerIface, QStringLiteral("PlaybackStatus")).toString();
    m_canPlay = getProperty(m_activeService, kPlayerIface, QStringLiteral("CanPlay")).toBool();
    m_canPause = getProperty(m_activeService, kPlayerIface, QStringLiteral("CanPause")).toBool();
    m_canGoNext = getProperty(m_activeService, kPlayerIface, QStringLiteral("CanGoNext")).toBool();
    m_canGoPrevious = getProperty(m_activeService, kPlayerIface, QStringLiteral("CanGoPrevious")).toBool();
    m_playerName = getProperty(m_activeService, kRootIface, QStringLiteral("Identity")).toString();
    m_desktopEntry = getProperty(m_activeService, kRootIface, QStringLiteral("DesktopEntry")).toString();

    const QVariant metaVariant = getProperty(m_activeService, kPlayerIface, QStringLiteral("Metadata"));
    QVariantMap meta;
    if (metaVariant.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        meta = qdbus_cast<QVariantMap>(metaVariant.value<QDBusArgument>());
    } else {
        meta = metaVariant.toMap();
    }
    m_title = variantToString(meta.value(QStringLiteral("xesam:title")));
    m_artist = variantToString(meta.value(QStringLiteral("xesam:artist")));
    m_album = variantToString(meta.value(QStringLiteral("xesam:album")));
    m_artUrl = variantToString(meta.value(QStringLiteral("mpris:artUrl")));

    emit changed();
}

void MprisModel::callPlayer(const QString &method)
{
    if (m_activeService.isEmpty()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_activeService, kObjectPath, kPlayerIface, method);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void MprisModel::playPause() { callPlayer(QStringLiteral("PlayPause")); }
void MprisModel::next() { callPlayer(QStringLiteral("Next")); }
void MprisModel::previous() { callPlayer(QStringLiteral("Previous")); }
void MprisModel::stop() { callPlayer(QStringLiteral("Stop")); }

QVariant MprisModel::playerProperty(const QString &service, const QString &name) const
{
    return getProperty(service, kPlayerIface, name);
}

qint64 MprisModel::activePid() const
{
    if (m_activeService.isEmpty()) {
        return -1;
    }
    const QDBusReply<uint> reply = QDBusConnection::sessionBus().interface()->servicePid(m_activeService);
    return reply.isValid() ? static_cast<qint64>(reply.value()) : -1;
}
