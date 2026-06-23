#include "networkmodel.h"

#include <QFile>
#include <QIODevice>
#include <QStringList>
#include <QtGlobal>

namespace {

constexpr int kMaxHistory = 24;

} // namespace

NetworkSampler::Bytes NetworkSampler::read()
{
    QFile file(QStringLiteral("/proc/net/dev"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    Bytes sample;
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

void NetworkSampler::start()
{
    m_elapsed.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &NetworkSampler::poll);
    m_timer->start();
    poll();
}

void NetworkSampler::poll()
{
    const Bytes sample = read();
    if (!sample.valid) {
        return;
    }

    if (!m_hasSample) {
        m_hasSample = true;
        m_last = sample;
        emit sampled(NetworkSample{0.0, 0.0});
        return;
    }

    const qint64 elapsedMs = qMax<qint64>(1, m_elapsed.restart());
    qint64 downloadDelta = sample.downloadBytes - m_last.downloadBytes;
    qint64 uploadDelta = sample.uploadBytes - m_last.uploadBytes;
    if (downloadDelta < 0) {
        downloadDelta = 0;
    }
    if (uploadDelta < 0) {
        uploadDelta = 0;
    }
    m_last = sample;

    emit sampled(NetworkSample{(downloadDelta * 1000.0) / static_cast<double>(elapsedMs),
                               (uploadDelta * 1000.0) / static_cast<double>(elapsedMs)});
}

NetworkModel::NetworkModel(QObject *parent)
    : QObject(parent)
    , m_sampler(new NetworkSampler)
{
    qRegisterMetaType<NetworkSample>();
    m_sampler->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_sampler, &NetworkSampler::start);
    connect(m_sampler, &NetworkSampler::sampled, this, &NetworkModel::onSampled);
    m_thread.start();
}

NetworkModel::~NetworkModel()
{
    m_thread.quit();
    m_thread.wait();
    delete m_sampler;
}

void NetworkModel::appendHistory(QVariantList *history, double rate)
{
    history->append(rate);
    while (history->size() > kMaxHistory) {
        history->removeFirst();
    }
}

double NetworkModel::computePeak(const QVariantList &history)
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

// Condense the 1 Hz samples into ~5s buckets, keeping ~5 minutes of them (a rotating ring).
void NetworkModel::accumulate5m(double download, double upload)
{
    constexpr int kBucketSamples = 5;  // 5 × 1s = one 5s bucket
    m_bucket5mDownSum += download;
    m_bucket5mUpSum += upload;
    ++m_bucket5mCount;
    if (m_bucket5mCount >= kBucketSamples) {
        m_download5m.append(m_bucket5mDownSum / m_bucket5mCount);
        m_upload5m.append(m_bucket5mUpSum / m_bucket5mCount);
        // Leave room for the live partial bucket the getter appends (kWindow5mPoints total).
        while (m_download5m.size() > kWindow5mPoints - 1) m_download5m.removeFirst();
        while (m_upload5m.size() > kWindow5mPoints - 1) m_upload5m.removeFirst();
        m_bucket5mDownSum = 0.0;
        m_bucket5mUpSum = 0.0;
        m_bucket5mCount = 0;
    }
}

void NetworkModel::onSampled(const NetworkSample &sample)
{
    m_downloadRateBytesPerSecond = sample.downloadRate;
    m_uploadRateBytesPerSecond = sample.uploadRate;
    m_totalRateBytesPerSecond = sample.downloadRate + sample.uploadRate;
    appendHistory(&m_downloadRateHistory, sample.downloadRate);
    appendHistory(&m_uploadRateHistory, sample.uploadRate);
    accumulate5m(sample.downloadRate, sample.uploadRate);
    m_downloadPeakBytesPerSecond = computePeak(m_downloadRateHistory);
    m_uploadPeakBytesPerSecond = computePeak(m_uploadRateHistory);
    emit statsChanged();
}
