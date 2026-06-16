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
    bool available() const { return true; }

signals:
    void statsChanged();

private slots:
    void onSampled(const NetworkSample &sample);

private:
    static void appendHistory(QVariantList *history, double rate);
    static double computePeak(const QVariantList &history);

    double m_downloadRateBytesPerSecond = 0.0;
    double m_uploadRateBytesPerSecond = 0.0;
    double m_totalRateBytesPerSecond = 0.0;
    double m_downloadPeakBytesPerSecond = 0.0;
    double m_uploadPeakBytesPerSecond = 0.0;
    QVariantList m_downloadRateHistory;
    QVariantList m_uploadRateHistory;

    QThread m_thread;
    NetworkSampler *m_sampler = nullptr;
};
