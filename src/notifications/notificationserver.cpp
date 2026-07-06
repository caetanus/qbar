#include "notificationserver.h"
#include "notificationwindow.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QImage>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QtDebug>

namespace {

// Decode the spec's raw-image hint: (iiibiiay) = width, height, rowstride,
// has_alpha, bits_per_sample, channels, data.
QImage decodeImageHint(const QVariant &hint)
{
    if (!hint.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument arg = hint.value<QDBusArgument>();
    int width = 0;
    int height = 0;
    int stride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;
    arg.beginStructure();
    arg >> width >> height >> stride >> hasAlpha >> bitsPerSample >> channels >> data;
    arg.endStructure();

    if (width <= 0 || height <= 0 || stride <= 0 || bitsPerSample != 8
        || (channels != 3 && channels != 4) || data.size() < (height - 1) * stride + width * channels) {
        return {};
    }

    const QImage::Format format = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    QImage image(width, height, format);
    const int rowBytes = width * channels;
    for (int y = 0; y < height; ++y) {
        memcpy(image.scanLine(y), data.constData() + static_cast<qsizetype>(y) * stride, rowBytes);
    }
    // Detach from `data` (scanLine already copied row by row) and normalise so the
    // provider can scale it cheaply.
    return image;
}

QVariant hintValue(const QVariantMap &hints, const char *modern, const char *legacy = nullptr)
{
    if (hints.contains(QLatin1String(modern))) {
        return hints.value(QLatin1String(modern));
    }
    if (legacy != nullptr && hints.contains(QLatin1String(legacy))) {
        return hints.value(QLatin1String(legacy));
    }
    return {};
}

// An icon reference from the wire (name, absolute path, or file:// url) → a source a
// QML Image can load. Icon names go through the existing themeicon provider.
QString iconToSource(const QString &icon)
{
    if (icon.isEmpty()) {
        return {};
    }
    if (icon.startsWith(QLatin1String("file://"))) {
        return icon;
    }
    if (icon.startsWith(QLatin1Char('/')) && QFileInfo::exists(icon)) {
        return QUrl::fromLocalFile(icon).toString();
    }
    return QStringLiteral("image://themeicon/") + icon;
}

} // namespace

NotificationsAdaptor::NotificationsAdaptor(NotificationServer *server)
    : QDBusAbstractAdaptor(server)
    , m_server(server)
{
    connect(server, &NotificationServer::notificationClosed,
            this, &NotificationsAdaptor::NotificationClosed);
    connect(server, &NotificationServer::actionInvoked,
            this, &NotificationsAdaptor::ActionInvoked);
}

uint NotificationsAdaptor::Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                                  const QString &summary, const QString &body, const QStringList &actions,
                                  const QVariantMap &hints, int expire_timeout)
{
    return m_server->notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout);
}

void NotificationsAdaptor::CloseNotification(uint id)
{
    m_server->closeNotification(id, NotificationServer::Closed);
}

QStringList NotificationsAdaptor::GetCapabilities()
{
    return {
        QStringLiteral("body"),
        QStringLiteral("body-markup"),
        QStringLiteral("body-hyperlinks"),
        QStringLiteral("actions"),
        QStringLiteral("icon-static"),
        QStringLiteral("persistence"),
        // Stack-tag coalescing (volume/brightness OSDs) — both spellings, so scripts
        // probing for either dunst's or notify-osd's capability find it.
        QStringLiteral("x-dunst-stack-tag"),
        QStringLiteral("x-canonical-private-synchronous"),
    };
}

QString NotificationsAdaptor::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = QStringLiteral("qbar");
    version = QStringLiteral("1.0");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("qbar");
}

NotificationServer *NotificationServer::s_instance = nullptr;

NotificationServer::NotificationServer(QQmlEngine *engine,
                                       QVariantMap theme,
                                       QVariantMap config,
                                       QObject *cssTheme,
                                       QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    s_instance = this;

    // The provider is owned (and deleted) by the engine.
    m_imageProvider = new NotificationImageProvider;
    if (engine->imageProvider(QStringLiteral("notifimage")) == nullptr) {
        engine->addImageProvider(QStringLiteral("notifimage"), m_imageProvider);
    }

    m_adaptor = new NotificationsAdaptor(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this)) {
        qWarning() << "QBar notifications: failed to register the D-Bus object";
    }
    m_registered = bus.registerService(QStringLiteral("org.freedesktop.Notifications"));
    if (!m_registered) {
        qWarning() << "QBar notifications: org.freedesktop.Notifications is already owned"
                   << "(dunst/mako running?) — the daemon is idle until the name frees up";
        // Grab the name when its current owner exits (e.g. the user stops dunst).
        auto *watcher = QDBusConnection::sessionBus().interface();
        connect(watcher, &QDBusConnectionInterface::serviceOwnerChanged, this,
                [this](const QString &name, const QString &, const QString &newOwner) {
                    if (m_registered || name != QLatin1String("org.freedesktop.Notifications")
                        || !newOwner.isEmpty()) {
                        return;
                    }
                    m_registered = QDBusConnection::sessionBus().registerService(name);
                    if (m_registered) {
                        qWarning() << "QBar notifications: took over org.freedesktop.Notifications";
                    }
                });
    }

    m_window = new NotificationWindow(engine, std::move(theme), m_config, &m_model, this, cssTheme, this);
}

NotificationServer::~NotificationServer()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.freedesktop.Notifications"));
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void NotificationServer::setBarWindow(QWindow *window)
{
    m_window->setBarWindow(window);
}

void NotificationServer::setConfig(const QVariantMap &config)
{
    m_config = config;
    m_window->setNotifConfig(config);
}

void NotificationServer::setDoNotDisturb(bool on)
{
    if (m_doNotDisturb == on) {
        return;
    }
    m_doNotDisturb = on;
    emit doNotDisturbChanged();
}

void NotificationServer::invokeAction(uint id, const QString &actionKey)
{
    emit actionInvoked(id, actionKey);
    const Notification *n = m_model.byId(id);
    if (n != nullptr && !n->resident) {
        closeNotification(id, Closed);
    }
}

void NotificationServer::dismiss(uint id)
{
    closeNotification(id, Dismissed);
}

void NotificationServer::dismissAll()
{
    // Snapshot: closeNotification mutates the model as it goes.
    const QList<quint32> ids = m_model.ids();
    for (quint32 id : ids) {
        closeNotification(id, Dismissed);
    }
}

void NotificationServer::setHovered(uint id, bool hovered)
{
    QTimer *timer = m_expiry.value(id);
    if (timer == nullptr) {
        return;
    }
    if (hovered && timer->isActive()) {
        timer->setProperty("remainingMs", timer->remainingTime());
        timer->stop();
    } else if (!hovered && !timer->isActive()) {
        const int remaining = timer->property("remainingMs").toInt();
        timer->start(qMax(500, remaining));
    }
}

uint NotificationServer::notify(const QString &appName, uint replacesId, const QString &appIcon,
                                const QString &summary, const QString &body, const QStringList &actions,
                                const QVariantMap &hints, int expireTimeout)
{
    // Stack tag (dunst / notify-osd volume-OSD idiom): a tagged notification without a
    // replaces_id coalesces into the live card carrying the same app + tag, instead of
    // stacking a new card per volume-key press. All the hint spellings in the wild:
    QString stackTag;
    for (const char *key : {"x-dunst-stack-tag", "x-canonical-private-synchronous",
                            "private-synchronous", "synchronous"}) {
        stackTag = hints.value(QLatin1String(key)).toString();
        if (!stackTag.isEmpty()) {
            break;
        }
    }

    quint32 id = replacesId;
    if (id == 0 && !stackTag.isEmpty()) {
        id = m_model.idByStackTag(appName, stackTag);
    }
    if (id == 0) {
        id = m_nextId++;
        if (m_nextId == 0) {
            m_nextId = 1;
        }
    }

    Notification n;
    n.id = id;
    n.stackTag = stackTag;
    n.appName = appName;
    n.summary = summary;
    n.body = body;
    n.timestamp = QDateTime::currentDateTime();

    const QVariant urgency = hintValue(hints, "urgency");
    n.urgency = urgency.isValid() ? qBound(0, urgency.toInt(), 2) : 1;
    n.category = hintValue(hints, "category").toString();
    n.transient = hintValue(hints, "transient").toBool();
    n.resident = hintValue(hints, "resident").toBool();
    const QVariant value = hintValue(hints, "value");
    n.value = value.isValid() ? qBound(0, value.toInt(), 100) : -1;

    // Wire actions are a flat [key, label, key, label, …] list. "default" is the
    // body-click action, not a button.
    for (int i = 0; i + 1 < actions.size(); i += 2) {
        if (actions.at(i) == QLatin1String("default")) {
            n.hasDefaultAction = true;
            continue;
        }
        n.actions.append(QVariantMap{
            {QStringLiteral("key"), actions.at(i)},
            {QStringLiteral("label"), actions.at(i + 1)},
        });
    }

    n.appIcon = resolveAppIcon(appIcon, hints, id);

    // Spec image priority: image-data > image-path > app_icon. The decoded pixmap is
    // published through the notifimage provider; the serial busts QML's image cache
    // on a replaces_id update.
    const QImage image = decodeImageHint(hintValue(hints, "image-data", "image_data"));
    if (!image.isNull()) {
        m_imageProvider->insert(id, image);
        n.imageSource = QStringLiteral("image://notifimage/%1/%2").arg(id).arg(++m_imageSerial);
    } else {
        const QString imagePath = hintValue(hints, "image-path", "image_path").toString();
        if (!imagePath.isEmpty()) {
            n.imageSource = iconToSource(imagePath);
        }
    }

    n.expireMs = resolveTimeout(expireTimeout, n.urgency);

    // Do-not-disturb: non-critical toasts are acknowledged (valid id, closed as
    // expired) but never shown.
    if (m_doNotDisturb && n.urgency < 2) {
        QTimer::singleShot(0, this, [this, id]() { emit notificationClosed(id, Expired); });
        return id;
    }

    m_model.upsert(n);
    disarmExpiry(id);
    if (n.expireMs > 0) {
        armExpiry(id, n.expireMs);
    }
    m_window->notificationArrived();
    return id;
}

void NotificationServer::closeNotification(uint id, CloseReason reason)
{
    disarmExpiry(id);
    if (!m_model.removeById(id)) {
        return;
    }
    m_imageProvider->remove(id);
    emit notificationClosed(id, reason);
}

void NotificationServer::armExpiry(quint32 id, int ms)
{
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id]() {
        closeNotification(id, Expired);
    });
    m_expiry.insert(id, timer);
    timer->start(ms);
}

void NotificationServer::disarmExpiry(quint32 id)
{
    if (QTimer *timer = m_expiry.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
}

QString NotificationServer::resolveAppIcon(const QString &appIcon, const QVariantMap &hints, quint32 id)
{
    Q_UNUSED(id);
    if (!appIcon.isEmpty()) {
        return iconToSource(appIcon);
    }
    // Fall back to the app's desktop-entry icon name when given.
    const QString desktopEntry = hintValue(hints, "desktop-entry").toString();
    if (!desktopEntry.isEmpty()) {
        return QStringLiteral("image://themeicon/") + desktopEntry.toLower();
    }
    return {};
}

int NotificationServer::resolveTimeout(int expireTimeout, int urgency) const
{
    if (expireTimeout > 0) {
        return expireTimeout;
    }
    if (expireTimeout == 0) {
        return 0; // never expires
    }
    // -1: server default, per urgency. Critical defaults to sticky (spec-recommended).
    switch (urgency) {
    case 0: return m_config.value(QStringLiteral("timeoutLow"), 4000).toInt();
    case 2: return m_config.value(QStringLiteral("timeoutCritical"), 0).toInt();
    default: return m_config.value(QStringLiteral("timeout"), 6000).toInt();
    }
}
