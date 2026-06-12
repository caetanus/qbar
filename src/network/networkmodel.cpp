#include "networkmodel.h"

#include <QFile>
#include <QIODevice>
#include <QStringList>
#include <QtGlobal>

namespace {

constexpr int kMaxHistory = 24;

} // namespace

NetworkModel::NetworkModel(QObject *parent)
    : QObject(parent)
{
    m_elapsed.start();
    refresh();
    QTimer::singleShot(1000, this, &NetworkModel::refresh);
    m_timer.setInterval(1000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &NetworkModel::refresh);
    m_timer.start();
}

double NetworkModel::downloadRateBytesPerSecond() const
{
    return m_downloadRateBytesPerSecond;
}

double NetworkModel::uploadRateBytesPerSecond() const
{
    return m_uploadRateBytesPerSecond;
}

double NetworkModel::totalRateBytesPerSecond() const
{
    return m_totalRateBytesPerSecond;
}

double NetworkModel::downloadPeakBytesPerSecond() const
{
    return m_downloadPeakBytesPerSecond;
}

double NetworkModel::uploadPeakBytesPerSecond() const
{
    return m_uploadPeakBytesPerSecond;
}

QVariantList NetworkModel::downloadRateHistory() const
{
    return m_downloadRateHistory;
}

QVariantList NetworkModel::uploadRateHistory() const
{
    return m_uploadRateHistory;
}

bool NetworkModel::available() const
{
    return true;
}

NetworkModel::TrafficSample NetworkModel::readTrafficSample() const
{
    QFile file(QStringLiteral("/proc/net/dev"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    TrafficSample sample;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Inter-")) || line.startsWith(QStringLiteral(" face"))) {
            continue;
        }

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }

        const QString iface = line.left(colon).trimmed();
        if (iface == QStringLiteral("lo")) {
            continue;
        }

        const QStringList parts = line.mid(colon + 1).split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 9) {
            continue;
        }

        bool okRx = false;
        bool okTx = false;
        const qint64 rxBytes = parts.at(0).toLongLong(&okRx);
        const qint64 txBytes = parts.at(8).toLongLong(&okTx);
        if (!okRx || !okTx) {
            continue;
        }

        sample.downloadBytes += qMax<qint64>(0, rxBytes);
        sample.uploadBytes += qMax<qint64>(0, txBytes);
        sample.valid = true;
    }

    return sample;
}

void NetworkModel::appendHistory(QVariantList *history, double rate)
{
    history->append(rate);
    while (history->size() > kMaxHistory) {
        history->removeFirst();
    }
}

double NetworkModel::computePeak(const QVariantList &history) const
{
    double peak = 0.0;
    for (const QVariant &value : history) {
        const double sample = value.toDouble();
        if (sample > peak) {
            peak = sample;
        }
    }
    return peak;
}

void NetworkModel::refresh()
{
    const TrafficSample sample = readTrafficSample();
    if (!sample.valid) {
        return;
    }

    if (!m_hasSample) {
        m_hasSample = true;
        m_lastSample = sample;
        m_downloadRateBytesPerSecond = 0.0;
        m_uploadRateBytesPerSecond = 0.0;
        m_totalRateBytesPerSecond = 0.0;
        appendHistory(&m_downloadRateHistory, 0.0);
        appendHistory(&m_uploadRateHistory, 0.0);
        m_downloadPeakBytesPerSecond = computePeak(m_downloadRateHistory);
        m_uploadPeakBytesPerSecond = computePeak(m_uploadRateHistory);
        emit statsChanged();
        return;
    }

    const qint64 elapsedMs = qMax<qint64>(1, m_elapsed.restart());
    qint64 downloadDelta = sample.downloadBytes - m_lastSample.downloadBytes;
    qint64 uploadDelta = sample.uploadBytes - m_lastSample.uploadBytes;
    if (downloadDelta < 0) {
        downloadDelta = 0;
    }
    if (uploadDelta < 0) {
        uploadDelta = 0;
    }

    m_lastSample = sample;
    m_downloadRateBytesPerSecond = (downloadDelta * 1000.0) / elapsedMs;
    m_uploadRateBytesPerSecond = (uploadDelta * 1000.0) / elapsedMs;
    m_totalRateBytesPerSecond = m_downloadRateBytesPerSecond + m_uploadRateBytesPerSecond;
    appendHistory(&m_downloadRateHistory, m_downloadRateBytesPerSecond);
    appendHistory(&m_uploadRateHistory, m_uploadRateBytesPerSecond);
    m_downloadPeakBytesPerSecond = computePeak(m_downloadRateHistory);
    m_uploadPeakBytesPerSecond = computePeak(m_uploadRateHistory);
    emit statsChanged();
}
