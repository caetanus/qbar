#include "diskmodel.h"

#include <QSet>
#include <QStorageInfo>
#include <QVariantMap>

#include <cmath>
#include <algorithm>

DiskModel::DiskModel(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(30000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &DiskModel::refresh);
    m_timer.start();
    refresh();
}

void DiskModel::setPath(const QString &path)
{
    if (m_path == path || path.isEmpty()) {
        return;
    }
    m_path = path;
    emit pathChanged();
    refresh();
}

QString DiskModel::humanSize(qint64 bytes)
{
    constexpr double kib = 1024.0;
    const double value = static_cast<double>(bytes);
    if (value >= kib * kib * kib * kib) {
        return QStringLiteral("%1 TB").arg(value / (kib * kib * kib * kib), 0, 'f', 1);
    }
    if (value >= kib * kib * kib) {
        return QStringLiteral("%1 GB").arg(value / (kib * kib * kib), 0, 'f', 1);
    }
    if (value >= kib * kib) {
        return QStringLiteral("%1 MB").arg(value / (kib * kib), 0, 'f', 0);
    }
    return QStringLiteral("%1 KB").arg(value / kib, 0, 'f', 0);
}

namespace {

QVariantList mountedVolumes()
{
    QVariantList mounts;
    QSet<QString> seenPaths;
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &a, const QStorageInfo &b) {
        return a.rootPath() < b.rootPath();
    });

    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0) {
            continue;
        }

        const QString path = volume.rootPath();
        if (path.isEmpty() || seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);

        const qint64 total = volume.bytesTotal();
        const qint64 free = volume.bytesAvailable();
        const qint64 used = total - free;
        const int percent = static_cast<int>(std::lround((static_cast<double>(used) * 100.0) / static_cast<double>(total)));

        QVariantMap item;
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("name"), volume.displayName().isEmpty() ? path : volume.displayName());
        item.insert(QStringLiteral("percent"), qBound(0, percent, 100));
        item.insert(QStringLiteral("usedBytes"), used);
        item.insert(QStringLiteral("totalBytes"), total);
        mounts.append(item);
    }

    return mounts;
}

} // namespace

void DiskModel::refresh()
{
    const QStorageInfo info(m_path);
    const bool ready = info.isValid() && info.isReady() && info.bytesTotal() > 0;
    const QVariantList mounts = mountedVolumes();
    int percent = 0;
    QString display;
    QString tooltip;

    if (ready) {
        const qint64 total = info.bytesTotal();
        const qint64 free = info.bytesAvailable();
        const qint64 used = total - free;
        percent = static_cast<int>(std::lround((static_cast<double>(used) * 100.0) / static_cast<double>(total)));
        display = QStringLiteral("%1%").arg(percent);
        tooltip = QStringLiteral("%1\n%2 free of %3")
                      .arg(m_path, humanSize(free), humanSize(total));
    } else {
        tooltip = QStringLiteral("%1 unavailable").arg(m_path);
    }

    const bool changedState = ready != m_available || percent != m_percent
        || display != m_displayText || tooltip != m_tooltipText || mounts != m_mounts;
    if (!changedState) {
        return;
    }
    m_available = ready;
    m_percent = percent;
    m_displayText = display;
    m_tooltipText = tooltip;
    m_mounts = mounts;
    emit changed();
}
