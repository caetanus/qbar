#include "notificationmodel.h"

#include <QDebug>
#include <QMutexLocker>

NotificationModel::NotificationModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NotificationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant NotificationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const Notification &n = m_items.at(index.row());
    switch (role) {
    case IdRole: return n.id;
    case AppNameRole: return n.appName;
    case AppIconRole: return n.appIcon;
    case SummaryRole: return n.summary;
    case BodyRole: return n.body;
    case UrgencyRole: return n.urgency;
    case ActionsRole: return n.actions;
    case HasDefaultActionRole: return n.hasDefaultAction;
    case ImageSourceRole: return n.imageSource;
    case ValueRole: return n.value;
    case CategoryRole: return n.category;
    case ExpireMsRole: return n.expireMs;
    case TimestampRole: return n.timestamp;
    }
    return {};
}

QHash<int, QByteArray> NotificationModel::roleNames() const
{
    return {
        {IdRole, "notifId"},
        {AppNameRole, "appName"},
        {AppIconRole, "appIcon"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {UrgencyRole, "urgency"},
        {ActionsRole, "actions"},
        {HasDefaultActionRole, "hasDefaultAction"},
        {ImageSourceRole, "imageSource"},
        {ValueRole, "value"},
        {CategoryRole, "category"},
        {ExpireMsRole, "expireMs"},
        {TimestampRole, "timestamp"},
    };
}

void NotificationModel::upsert(const Notification &notification)
{
    const int row = rowOf(notification.id);
    if (row >= 0) {
        qInfo() << "[notif] model update id" << notification.id << "row" << row;
        m_items[row] = notification;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
        return;
    }
    qInfo() << "[notif] model insert id" << notification.id;
    beginInsertRows(QModelIndex(), 0, 0);
    m_items.prepend(notification);
    endInsertRows();
    emit countChanged();
}

bool NotificationModel::removeById(quint32 id)
{
    const int row = rowOf(id);
    if (row < 0) {
        return false;
    }
    qInfo() << "[notif] model remove id" << id << "row" << row;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    return true;
}

QList<quint32> NotificationModel::removeAll()
{
    if (m_items.isEmpty()) {
        return {};
    }
    QList<quint32> removed = ids();
    qInfo() << "[notif] model remove all (" << m_items.size() << "rows)";
    beginRemoveRows(QModelIndex(), 0, static_cast<int>(m_items.size()) - 1);
    m_items.clear();
    endRemoveRows();
    emit countChanged();
    return removed;
}

const Notification *NotificationModel::byId(quint32 id) const
{
    const int row = rowOf(id);
    return row >= 0 ? &m_items.at(row) : nullptr;
}

QList<quint32> NotificationModel::ids() const
{
    QList<quint32> out;
    out.reserve(m_items.size());
    for (const Notification &n : m_items) {
        out.append(n.id);
    }
    return out;
}

quint32 NotificationModel::idByStackTag(const QString &appName, const QString &tag) const
{
    for (const Notification &n : m_items) {
        if (n.stackTag == tag && n.appName == appName) {
            return n.id;
        }
    }
    return 0;
}

int NotificationModel::rowOf(quint32 id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QImage NotificationImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Url shape: image://notifimage/<id>/<serial> — the serial only busts the cache.
    const quint32 notifId = id.section(QLatin1Char('/'), 0, 0).toUInt();
    QImage image;
    {
        QMutexLocker locker(&m_mutex);
        image = m_images.value(notifId);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void NotificationImageProvider::insert(quint32 id, const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    m_images.insert(id, image);
}

void NotificationImageProvider::remove(quint32 id)
{
    QMutexLocker locker(&m_mutex);
    m_images.remove(id);
}
