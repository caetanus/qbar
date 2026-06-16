#include "diskmodel.h"

#include <QStorageInfo>

#include <cmath>

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

void DiskModel::refresh()
{
    const QStorageInfo info(m_path);
    const bool ready = info.isValid() && info.isReady() && info.bytesTotal() > 0;
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
        || display != m_displayText || tooltip != m_tooltipText;
    if (!changedState) {
        return;
    }
    m_available = ready;
    m_percent = percent;
    m_displayText = display;
    m_tooltipText = tooltip;
    emit changed();
}
