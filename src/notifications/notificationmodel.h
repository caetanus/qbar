#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantList>

// One org.freedesktop.Notifications notification, demarshalled. `actions` is a list of
// { "key", "label" } maps (the wire format is a flat [key, label, ...] list); the
// "default" key is split out into hasDefaultAction — it is invoked by clicking the card
// body, never shown as a button.
struct Notification {
    quint32 id = 0;
    QString appName;
    QString appIcon;    // resolved to a QML-usable source (file://, image://themeicon/…)
    QString summary;
    QString body;       // Pango-ish markup; rendered with Text.StyledText
    int urgency = 1;    // 0 low, 1 normal, 2 critical
    QVariantList actions;
    bool hasDefaultAction = false;
    QString imageSource; // image://notifimage/<id>/<serial> when the hints carry an image
    int value = -1;      // "value" hint (0..100 volume/brightness OSD), -1 = none
    // Stack tag (dunst's x-dunst-stack-tag / notify-osd's synchronous hints): two live
    // notifications with the same app + tag coalesce into one card (volume OSD pattern).
    QString stackTag;
    QString category;
    bool transient = false;
    bool resident = false;
    int expireMs = -1;   // resolved timeout (ms), 0 = never
    QDateTime timestamp;
};

class NotificationModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        AppIconRole,
        SummaryRole,
        BodyRole,
        UrgencyRole,
        ActionsRole,
        HasDefaultActionRole,
        ImageSourceRole,
        ValueRole,
        CategoryRole,
        ExpireMsRole,
        TimestampRole,
    };

    explicit NotificationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_items.size()); }

    // Insert at row 0 (newest first), or update the row carrying the same id in
    // place (the spec's replaces_id path — no re-entry animation).
    void upsert(const Notification &notification);
    bool removeById(quint32 id);
    const Notification *byId(quint32 id) const;
    QList<quint32> ids() const;
    // Id of the live notification carrying the same app + stack tag (0 when none) —
    // the coalescing target for a tagged notification without a replaces_id.
    quint32 idByStackTag(const QString &appName, const QString &tag) const;

signals:
    void countChanged();

private:
    int rowOf(quint32 id) const;

    QList<Notification> m_items;
};

// Serves the decoded `image-data`/`image-path` hint pixmaps to QML as
// image://notifimage/<id>/<serial>. The serial makes a replaces_id update a NEW url,
// or the QML Image cache would keep showing the old frame.
class NotificationImageProvider final : public QQuickImageProvider {
public:
    NotificationImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // insert/remove run on the GUI thread; requestImage on the QML image-loader
    // thread — hence the mutex.
    void insert(quint32 id, const QImage &image);
    void remove(quint32 id);

private:
    QMutex m_mutex;
    QHash<quint32, QImage> m_images;
};
