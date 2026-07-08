#pragma once

#include "notificationmodel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class QQmlEngine;
class QWindow;
class NotificationServer;
class NotificationWindow;
class QTimer;

// The org.freedesktop.Notifications interface (Desktop Notifications spec 1.2),
// forwarded onto NotificationServer. Method/arg casing is fixed by the spec.
class NotificationsAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationsAdaptor(NotificationServer *server);

public slots:
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expire_timeout);
    void CloseNotification(uint id);
    QStringList GetCapabilities();
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

signals:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);
    // KDE inline-reply extension (capability "inline-reply"): the client gets the
    // typed text back through this signal instead of a plain ActionInvoked.
    void NotificationReplied(uint id, const QString &text);

private:
    NotificationServer *m_server;
};

// Owns the D-Bus service, the notification model, the expiry timers and the toast
// window. ONE per process (the first BarWindow whose config enables notifications
// creates it) — a second instance could not register the bus name anyway.
class NotificationServer final : public QObject {
    Q_OBJECT
    // Global do-not-disturb: toasts are suppressed (closed as expired on arrival)
    // while set; critical notifications still show.
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)

public:
    // NotificationClosed reason codes (spec).
    enum CloseReason { Expired = 1, Dismissed = 2, Closed = 3 };

    explicit NotificationServer(QQmlEngine *engine,
                                QVariantMap theme,
                                QVariantMap config,
                                QObject *cssTheme,
                                QObject *parent = nullptr);
    ~NotificationServer() override;

    static NotificationServer *instance() { return s_instance; }

    NotificationModel *model() { return &m_model; }
    void setBarWindow(QWindow *window);
    void setConfig(const QVariantMap &config);

    bool doNotDisturb() const { return m_doNotDisturb; }
    void setDoNotDisturb(bool on);

    // Rich-card config switches (notifications.actionButtons / .inlineReply) —
    // both off makes plain text toasts; the capability list follows them live.
    bool actionButtonsEnabled() const
    {
        return m_config.value(QStringLiteral("actionButtons"), true).toBool();
    }
    bool inlineReplyEnabled() const
    {
        return m_config.value(QStringLiteral("inlineReply"), true).toBool();
    }

    // QML-side interactions (card click, action button, close button, hover).
    Q_INVOKABLE void invokeAction(uint id, const QString &actionKey);
    Q_INVOKABLE void dismiss(uint id);
    Q_INVOKABLE void dismissAll();
    Q_INVOKABLE void setHovered(uint id, bool hovered);
    // Inline reply: emits NotificationReplied and dismisses the card.
    Q_INVOKABLE void reply(uint id, const QString &text);
    // An open reply field pauses the expiry (like hover, but hover-independent)
    // and asks the layer surface for on-demand keyboard focus while any is open.
    Q_INVOKABLE void setReplying(uint id, bool replying);
    bool replyActive() const { return !m_replying.isEmpty(); }

    // Adaptor entry points.
    uint notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expireTimeout);
    void closeNotification(uint id, CloseReason reason);

signals:
    void doNotDisturbChanged();
    // Relayed onto the adaptor's D-Bus signals (signal→signal connect).
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &actionKey);
    void notificationReplied(uint id, const QString &text);
    void replyActiveChanged(bool active);

private:
    void armExpiry(quint32 id, int ms);
    void disarmExpiry(quint32 id);
    QString resolveAppIcon(const QString &appIcon, const QVariantMap &hints, quint32 id);
    int resolveTimeout(int expireTimeout, int urgency) const;

    static NotificationServer *s_instance;

    NotificationModel m_model;
    NotificationsAdaptor *m_adaptor = nullptr;
    NotificationWindow *m_window = nullptr;
    NotificationImageProvider *m_imageProvider = nullptr; // owned by the QML engine
    QVariantMap m_config;
    QHash<quint32, QTimer *> m_expiry;
    QSet<quint32> m_replying;
    quint32 m_nextId = 1;
    quint32 m_imageSerial = 0;
    bool m_doNotDisturb = false;
    bool m_registered = false;
};
