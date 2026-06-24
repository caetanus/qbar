#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QVariantList>
#include <QStringList>
#include <QVector>

class CpuModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int usage READ usage NOTIFY usageChanged)
    Q_PROPERTY(QVariantList usageHistory READ usageHistory NOTIFY usageHistoryChanged)
    Q_PROPERTY(double clockMhz READ clockMhz NOTIFY clockChanged)
    Q_PROPERTY(double loadAverage1 READ loadAverage1 NOTIFY loadAverageChanged)
    Q_PROPERTY(double loadAverage5 READ loadAverage5 NOTIFY loadAverageChanged)
    Q_PROPERTY(double loadAverage15 READ loadAverage15 NOTIFY loadAverageChanged)
    Q_PROPERTY(QVariantList loadAverageHistory READ loadAverageHistory NOTIFY loadAverageHistoryChanged)
    Q_PROPERTY(int processCount READ processCount NOTIFY processStatsChanged)
    Q_PROPERTY(int runningProcesses READ runningProcesses NOTIFY processStatsChanged)
    Q_PROPERTY(QVariantList topProcesses READ topProcesses NOTIFY processStatsChanged)
    Q_PROPERTY(QVariantList topMemoryProcesses READ topMemoryProcesses NOTIFY processStatsChanged)
    Q_PROPERTY(int memoryUsage READ memoryUsage NOTIFY memoryStatsChanged)
    Q_PROPERTY(qint64 memoryUsedBytes READ memoryUsedBytes NOTIFY memoryStatsChanged)
    Q_PROPERTY(qint64 memoryTotalBytes READ memoryTotalBytes NOTIFY memoryStatsChanged)
    Q_PROPERTY(qint64 memoryFreeBytes READ memoryFreeBytes NOTIFY memoryStatsChanged)
    Q_PROPERTY(QVariantList memoryUsageHistory READ memoryUsageHistory NOTIFY memoryStatsChanged)
    Q_PROPERTY(int swapUsage READ swapUsage NOTIFY memoryStatsChanged)
    Q_PROPERTY(QVariantList swapUsageHistory READ swapUsageHistory NOTIFY memoryStatsChanged)
    Q_PROPERTY(QVariantList cores READ cores NOTIFY coresChanged)
    Q_PROPERTY(int coreCount READ coreCount NOTIFY coresChanged)
    Q_PROPERTY(int historyWindow READ historyWindow CONSTANT)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit CpuModel(QObject *parent = nullptr);

    int usage() const;
    QVariantList usageHistory() const;
    double clockMhz() const;
    double loadAverage1() const;
    double loadAverage5() const;
    double loadAverage15() const;
    QVariantList loadAverageHistory() const;
    int processCount() const;
    int runningProcesses() const;
    QVariantList topProcesses() const;
    QVariantList topMemoryProcesses() const;
    int memoryUsage() const;
    qint64 memoryUsedBytes() const;
    qint64 memoryTotalBytes() const;
    qint64 memoryFreeBytes() const;
    QVariantList memoryUsageHistory() const;
    int swapUsage() const;
    QVariantList swapUsageHistory() const;
    QVariantList cores() const;
    int coreCount() const;
    int historyWindow() const;
    bool available() const;

    Q_INVOKABLE QString coreName(int index) const;
    Q_INVOKABLE int coreUsage(int index) const;
    Q_INVOKABLE QVariantList coreHistory(int index) const;

signals:
    void usageChanged();
    void usageHistoryChanged();
    void clockChanged();
    void loadAverageChanged();
    void loadAverageHistoryChanged();
    void processStatsChanged();
    void memoryStatsChanged();
    void coresChanged();

private:
    struct Sample {
        QString name;
        qint64 total = 0;
        qint64 idle = 0;
        bool valid = false;
    };

    struct CoreState {
        QString name;
        qint64 total = 0;
        qint64 idle = 0;
        int usage = 0;
        QVariantList history;
        bool valid = false;
    };

    struct LoadAverageSample {
        double one = 0.0;
        double five = 0.0;
        double fifteen = 0.0;
        bool valid = false;
    };

    struct ProcessSample {
        int pid = 0;
        QString name;
        qint64 ticks = 0;
        qint64 rssKb = 0;
        bool valid = false;
    };

    QVector<Sample> readSamples() const;
    LoadAverageSample readLoadAverage() const;
    QVector<ProcessSample> readProcessSamples() const;
    int readRunningProcesses() const;
    int readMemoryUsage(qint64 *usedKb = nullptr, qint64 *totalKb = nullptr,
                        qint64 *freeKb = nullptr) const;
    int readSwapUsage() const;
    double readClockMhz() const;
    void refresh();
    void appendUsage(int usage);
    void appendLoadAverage(double loadAverage);
    void appendMemoryUsage(int usage);
    void appendSwapUsage(int usage);
    void appendCoreUsage(int index, int usage);
    void resetSamples(const QVector<Sample> &samples);
    void updateProcessStats(const QVector<ProcessSample> &processes, qint64 totalDiff);
    qint64 readProcessRssKb(int pid) const;
    qint64 readProcessPssKb(int pid) const;

    int m_usage = 0;
    double m_clockMhz = 0.0;
    QVariantList m_usageHistory;
    double m_loadAverage1 = 0.0;
    double m_loadAverage5 = 0.0;
    double m_loadAverage15 = 0.0;
    QVariantList m_loadAverageHistory;
    int m_runningProcesses = 0;
    int m_processCount = 0;
    QVariantList m_topProcesses;
    QVariantList m_topMemoryProcesses;
    int m_memoryUsage = 0;
    qint64 m_memoryUsedKb = 0;
    qint64 m_memoryTotalKb = 0;
    qint64 m_memoryFreeKb = 0;
    QVariantList m_memoryUsageHistory;
    int m_swapUsage = 0;
    QVariantList m_swapUsageHistory;
    QVector<CoreState> m_cores;
    Sample m_lastSample;
    bool m_hasSample = false;
    QHash<int, qint64> m_lastProcessTicks;
    QTimer m_timer;
};
