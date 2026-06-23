#pragma once

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>

// One tick's computed rates — the whole summary in a single payload so a single
// queued signal crosses to the GUI thread per second (one wakeup), never more.
struct NetworkSample {
    double downloadRate = 0.0;
    double uploadRate = 0.0;
};
Q_DECLARE_METATYPE(NetworkSample)

// Lives on a dedicated worker thread: samples /proc/net/dev on its own timer and
// emits the per-tick rates via a single queued signal, so reading/parsing a
// large interface table never touches the GUI thread.
class NetworkSampler final : public QObject {
    Q_OBJECT

public slots:
    void start(); // creates and starts the timer in the worker thread
    void poll();

signals:
    void sampled(NetworkSample sample);

private:
    struct Bytes {
        qint64 downloadBytes = 0;
        qint64 uploadBytes = 0;
        bool valid = false;
    };

    static Bytes read();

    Bytes m_last;
    bool m_hasSample = false;
    QElapsedTimer m_elapsed;
    QTimer *m_timer = nullptr;
};

class NetworkModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double downloadRateBytesPerSecond READ downloadRateBytesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double uploadRateBytesPerSecond READ uploadRateBytesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double totalRateBytesPerSecond READ totalRateBytesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double downloadPeakBytesPerSecond READ downloadPeakBytesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double uploadPeakBytesPerSecond READ uploadPeakBytesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(QVariantList downloadRateHistory READ downloadRateHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList uploadRateHistory READ uploadRateHistory NOTIFY statsChanged)
    // Condensed rolling 5-minute window (≤60 points, each a ~5s bucket average; the partial
    // current bucket is the live last point) — for the network popup's history graph.
    Q_PROPERTY(QVariantList downloadHistory5m READ downloadHistory5m NOTIFY statsChanged)
    Q_PROPERTY(QVariantList uploadHistory5m READ uploadHistory5m NOTIFY statsChanged)
    Q_PROPERTY(int historyWindowSeconds READ historyWindowSeconds NOTIFY statsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit NetworkModel(QObject *parent = nullptr);
    ~NetworkModel() override;

    double downloadRateBytesPerSecond() const { return m_downloadRateBytesPerSecond; }
    double uploadRateBytesPerSecond() const { return m_uploadRateBytesPerSecond; }
    double totalRateBytesPerSecond() const { return m_totalRateBytesPerSecond; }
    double downloadPeakBytesPerSecond() const { return m_downloadPeakBytesPerSecond; }
    double uploadPeakBytesPerSecond() const { return m_uploadPeakBytesPerSecond; }
    QVariantList downloadRateHistory() const { return m_downloadRateHistory; }
    QVariantList uploadRateHistory() const { return m_uploadRateHistory; }
    // The X axis grows in tiers for better early visibility: it starts at 1 min and steps to
    // 2, then 3, then 5 minutes as the collected data fills the current tier (rather than
    // showing a mostly-empty 5-min axis up front). 5s buckets → 12/24/36/60 points.
    static constexpr int kWindow5mPoints = 60; // 5-minute cap
    QVariantList downloadHistory5m() const { return window5m(m_download5m, m_bucket5mDownSum); }
    QVariantList uploadHistory5m() const { return window5m(m_upload5m, m_bucket5mUpSum); }
    // Current adaptive window length, in seconds (60/120/180/300) — for the graph's axis label.
    int historyWindowSeconds() const { return windowTier(collected5m()) * 5; }
    bool available() const { return true; }

signals:
    void statsChanged();

private slots:
    void onSampled(const NetworkSample &sample);

private:
    static void appendHistory(QVariantList *history, double rate);
    static double computePeak(const QVariantList &history);
    void accumulate5m(double download, double upload);
    // Number of points currently held (completed buckets + the live partial bucket).
    int collected5m() const
    {
        return static_cast<int>(m_download5m.size()) + (m_bucket5mCount > 0 ? 1 : 0);
    }
    // Adaptive window: smallest tier (1/2/3/5 min in 5s buckets) that holds `collected`.
    static int windowTier(int collected)
    {
        if (collected <= 12) { return 12; }  // 1 min
        if (collected <= 24) { return 24; }  // 2 min
        if (collected <= 36) { return 36; }  // 3 min
        return 60;                           // 5 min (cap)
    }
    QVariantList window5m(const QVariantList &completed, double partialSum) const
    {
        QVariantList out = completed;
        if (m_bucket5mCount > 0) {
            out.append(partialSum / m_bucket5mCount);
        }
        const int tier = windowTier(static_cast<int>(out.size()));
        while (out.size() < tier) {
            out.prepend(0.0);
        }
        return out;
    }

    double m_downloadRateBytesPerSecond = 0.0;
    double m_uploadRateBytesPerSecond = 0.0;
    double m_totalRateBytesPerSecond = 0.0;
    double m_downloadPeakBytesPerSecond = 0.0;
    double m_uploadPeakBytesPerSecond = 0.0;
    QVariantList m_downloadRateHistory;
    QVariantList m_uploadRateHistory;
    // 5-minute condensed window: completed bucket averages + the in-progress accumulator.
    QVariantList m_download5m;
    QVariantList m_upload5m;
    double m_bucket5mDownSum = 0.0;
    double m_bucket5mUpSum = 0.0;
    int m_bucket5mCount = 0;

    QThread m_thread;
    NetworkSampler *m_sampler = nullptr;
};
