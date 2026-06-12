#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>

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

    double downloadRateBytesPerSecond() const;
    double uploadRateBytesPerSecond() const;
    double totalRateBytesPerSecond() const;
    double downloadPeakBytesPerSecond() const;
    double uploadPeakBytesPerSecond() const;
    QVariantList downloadRateHistory() const;
    QVariantList uploadRateHistory() const;
    bool available() const;

signals:
    void statsChanged();

private:
    struct TrafficSample {
        qint64 downloadBytes = 0;
        qint64 uploadBytes = 0;
        bool valid = false;
    };

    TrafficSample readTrafficSample() const;
    void appendHistory(QVariantList *history, double rate);
    double computePeak(const QVariantList &history) const;
    void refresh();

    double m_downloadRateBytesPerSecond = 0.0;
    double m_uploadRateBytesPerSecond = 0.0;
    double m_totalRateBytesPerSecond = 0.0;
    double m_downloadPeakBytesPerSecond = 0.0;
    double m_uploadPeakBytesPerSecond = 0.0;
    QVariantList m_downloadRateHistory;
    QVariantList m_uploadRateHistory;
    TrafficSample m_lastSample;
    bool m_hasSample = false;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
};
