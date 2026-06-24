#include "cpumodel.h"

#include <QFile>
#include <QDir>
#include <QIODevice>
#include <QtGlobal>
#include <QVariantMap>
#include <algorithm>

namespace {

constexpr int kMaxHistory = 24;

} // namespace

CpuModel::CpuModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    QTimer::singleShot(1000, this, &CpuModel::refresh);
    m_timer.setInterval(5000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &CpuModel::refresh);
    m_timer.start();
}

int CpuModel::usage() const
{
    return m_usage;
}

QVariantList CpuModel::usageHistory() const
{
    return m_usageHistory;
}

double CpuModel::clockMhz() const
{
    return m_clockMhz;
}

double CpuModel::loadAverage1() const
{
    return m_loadAverage1;
}

double CpuModel::loadAverage5() const
{
    return m_loadAverage5;
}

double CpuModel::loadAverage15() const
{
    return m_loadAverage15;
}

QVariantList CpuModel::loadAverageHistory() const
{
    return m_loadAverageHistory;
}

int CpuModel::runningProcesses() const
{
    return m_runningProcesses;
}

int CpuModel::processCount() const
{
    return m_processCount;
}

QVariantList CpuModel::topProcesses() const
{
    return m_topProcesses;
}

QVariantList CpuModel::topMemoryProcesses() const
{
    return m_topMemoryProcesses;
}

int CpuModel::memoryUsage() const
{
    return m_memoryUsage;
}

qint64 CpuModel::memoryUsedBytes() const
{
    return m_memoryUsedKb * 1024;
}

qint64 CpuModel::memoryTotalBytes() const
{
    return m_memoryTotalKb * 1024;
}

QVariantList CpuModel::memoryUsageHistory() const
{
    return m_memoryUsageHistory;
}

int CpuModel::swapUsage() const
{
    return m_swapUsage;
}

QVariantList CpuModel::swapUsageHistory() const
{
    return m_swapUsageHistory;
}

QVariantList CpuModel::cores() const
{
    QVariantList result;
    result.reserve(m_cores.size());
    for (const CoreState &core : m_cores) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), core.name);
        item.insert(QStringLiteral("usage"), core.usage);
        item.insert(QStringLiteral("history"), core.history);
        result.append(item);
    }
    return result;
}

int CpuModel::coreCount() const
{
    return m_cores.size();
}

int CpuModel::historyWindow() const
{
    return kMaxHistory;
}

bool CpuModel::available() const
{
    return true;
}

QString CpuModel::coreName(int index) const
{
    if (index < 0 || index >= m_cores.size()) {
        return {};
    }

    return m_cores.at(index).name;
}

int CpuModel::coreUsage(int index) const
{
    if (index < 0 || index >= m_cores.size()) {
        return 0;
    }

    return m_cores.at(index).usage;
}

QVariantList CpuModel::coreHistory(int index) const
{
    if (index < 0 || index >= m_cores.size()) {
        return {};
    }

    return m_cores.at(index).history;
}

QVector<CpuModel::Sample> CpuModel::readSamples() const
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QVector<Sample> samples;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("cpu"))) {
            continue;
        }

        const QStringList parts = line.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 5) {
            continue;
        }

        qint64 total = 0;
        bool okAny = false;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok = false;
            const qint64 value = parts.at(i).toLongLong(&ok);
            if (ok) {
                total += value;
                okAny = true;
            }
        }
        if (!okAny) {
            continue;
        }

        bool okIdle = false;
        const qint64 idle = parts.at(4).toLongLong(&okIdle)
            + (parts.size() > 5 ? parts.at(5).toLongLong() : 0);
        if (!okIdle) {
            continue;
        }

        samples.append(Sample{parts.first(), total, idle, true});
    }

    return samples;
}

CpuModel::LoadAverageSample CpuModel::readLoadAverage() const
{
    QFile file(QStringLiteral("/proc/loadavg"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const QStringList parts = line.split(QChar::Space, Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return {};
    }

    bool ok1 = false;
    bool ok5 = false;
    bool ok15 = false;
    const double one = parts.at(0).toDouble(&ok1);
    const double five = parts.at(1).toDouble(&ok5);
    const double fifteen = parts.at(2).toDouble(&ok15);
    if (!ok1 || !ok5 || !ok15) {
        return {};
    }

    return LoadAverageSample{one, five, fifteen, true};
}

QVector<CpuModel::ProcessSample> CpuModel::readProcessSamples() const
{
    QVector<ProcessSample> processes;
    const QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    processes.reserve(entries.size());

    for (const QString &entry : entries) {
        bool ok = false;
        const int pid = entry.toInt(&ok);
        if (!ok) {
            continue;
        }

        QFile statFile(QStringLiteral("/proc/%1/stat").arg(entry));
        if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QString statLine = QString::fromUtf8(statFile.readAll()).trimmed();
        const int closeParen = statLine.lastIndexOf(QLatin1Char(')'));
        if (closeParen < 0 || closeParen + 2 >= statLine.size()) {
            continue;
        }

        const QString afterComm = statLine.mid(closeParen + 2);
        const QStringList parts = afterComm.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 13) {
            continue;
        }

        bool okUtime = false;
        bool okStime = false;
        const qint64 utime = parts.at(11).toLongLong(&okUtime);
        const qint64 stime = parts.at(12).toLongLong(&okStime);
        if (!okUtime || !okStime) {
            continue;
        }

        QFile commFile(QStringLiteral("/proc/%1/comm").arg(entry));
        QString name = QStringLiteral("pid %1").arg(pid);
        if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString comm = QString::fromUtf8(commFile.readLine()).trimmed();
            if (!comm.isEmpty()) {
                name = comm;
            }
        }

        processes.append(ProcessSample{pid, name, utime + stime, readProcessRssKb(pid), true});
    }

    return processes;
}

qint64 CpuModel::readProcessRssKb(int pid) const
{
    QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
    if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    const QString content = QString::fromUtf8(statusFile.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("VmRSS:"))) {
            continue;
        }

        const QStringList parts = line.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            return 0;
        }

        bool ok = false;
        const qint64 rssKb = parts.at(1).toLongLong(&ok);
        return ok ? qMax<qint64>(0, rssKb) : 0;
    }

    return 0;
}

qint64 CpuModel::readProcessPssKb(int pid) const
{
    // PSS (Proportional Set Size) from smaps_rollup: shared pages (libraries, shared
    // memory) are split across the processes that map them, so a Qt app no longer
    // looks like it owns every toolkit it merely shares. RSS counts shared pages in
    // full for *every* process — that's why a 190 MB app reads as 900 MB. Returns 0
    // when smaps_rollup is unreadable (pre-4.14 kernels, or another user's process
    // without privilege) so the caller can fall back to RSS.
    QFile rollup(QStringLiteral("/proc/%1/smaps_rollup").arg(pid));
    if (!rollup.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    const QString content = QString::fromUtf8(rollup.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("Pss:"))) {
            continue;
        }

        const QStringList parts = line.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            return 0;
        }

        bool ok = false;
        const qint64 pssKb = parts.at(1).toLongLong(&ok);
        return ok ? qMax<qint64>(0, pssKb) : 0;
    }

    return 0;
}

int CpuModel::readRunningProcesses() const
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("procs_running"))) {
            continue;
        }

        const QStringList parts = line.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool ok = false;
            const int running = parts.at(1).toInt(&ok);
            if (ok) {
                return running;
            }
        }
    }

    return 0;
}

int CpuModel::readMemoryUsage(qint64 *usedKb, qint64 *totalKb) const
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    qint64 memTotal = 0;
    qint64 memAvailable = -1;
    qint64 memFree = 0;
    qint64 buffers = 0;
    qint64 cached = 0;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        auto valueFor = [](const QString &candidate) -> qint64 {
            const QStringList parts = candidate.split(QChar::Space, Qt::SkipEmptyParts);
            if (parts.size() < 2) {
                return 0;
            }
            bool ok = false;
            const qint64 value = parts.at(1).toLongLong(&ok);
            return ok ? value : 0;
        };

        if (line.startsWith(QStringLiteral("MemTotal:"))) {
            memTotal = valueFor(line);
        } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
            memAvailable = valueFor(line);
        } else if (line.startsWith(QStringLiteral("MemFree:"))) {
            memFree = valueFor(line);
        } else if (line.startsWith(QStringLiteral("Buffers:"))) {
            buffers = valueFor(line);
        } else if (line.startsWith(QStringLiteral("Cached:"))) {
            cached = valueFor(line);
        }
    }

    if (memTotal <= 0) {
        return 0;
    }

    const qint64 available = memAvailable >= 0 ? memAvailable : (memFree + buffers + cached);
    const qint64 used = qMax<qint64>(0, memTotal - available);
    if (usedKb != nullptr) {
        *usedKb = used;
    }
    if (totalKb != nullptr) {
        *totalKb = memTotal;
    }
    return qBound(0, static_cast<int>((used * 100.0) / memTotal), 100);
}

double CpuModel::readClockMhz() const
{
    // Average the per-core "cpu MHz" lines from /proc/cpuinfo (current frequency
    // on most kernels). Returns 0 when unavailable.
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0.0;
    }

    double sum = 0.0;
    int count = 0;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QStringLiteral("cpu MHz"))) {
            continue;
        }
        const int colon = line.indexOf(QChar::fromLatin1(':'));
        if (colon < 0) {
            continue;
        }
        bool ok = false;
        const double value = line.mid(colon + 1).trimmed().toDouble(&ok);
        if (ok) {
            sum += value;
            count += 1;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

int CpuModel::readSwapUsage() const
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    qint64 swapTotal = 0;
    qint64 swapFree = 0;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QChar::LineFeed, Qt::SkipEmptyParts);
    auto valueFor = [](const QString &candidate) -> qint64 {
        const QStringList parts = candidate.split(QChar::Space, Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            return 0;
        }
        bool ok = false;
        const qint64 value = parts.at(1).toLongLong(&ok);
        return ok ? value : 0;
    };

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("SwapTotal:"))) {
            swapTotal = valueFor(line);
        } else if (line.startsWith(QStringLiteral("SwapFree:"))) {
            swapFree = valueFor(line);
        }
    }

    if (swapTotal <= 0) {
        return 0;
    }

    const qint64 used = qMax<qint64>(0, swapTotal - swapFree);
    return qBound(0, static_cast<int>((used * 100.0) / swapTotal), 100);
}

void CpuModel::appendUsage(int usage)
{
    m_usageHistory.append(usage);
    while (m_usageHistory.size() > kMaxHistory) {
        m_usageHistory.removeFirst();
    }
    emit usageHistoryChanged();
}

void CpuModel::appendLoadAverage(double loadAverage)
{
    m_loadAverageHistory.append(loadAverage);
    while (m_loadAverageHistory.size() > kMaxHistory) {
        m_loadAverageHistory.removeFirst();
    }
    emit loadAverageHistoryChanged();
}

void CpuModel::appendMemoryUsage(int usage)
{
    m_memoryUsageHistory.append(usage);
    while (m_memoryUsageHistory.size() > kMaxHistory) {
        m_memoryUsageHistory.removeFirst();
    }
    emit memoryStatsChanged();
}

void CpuModel::appendSwapUsage(int usage)
{
    m_swapUsageHistory.append(usage);
    while (m_swapUsageHistory.size() > kMaxHistory) {
        m_swapUsageHistory.removeFirst();
    }
    emit memoryStatsChanged();
}

void CpuModel::appendCoreUsage(int index, int usage)
{
    if (index < 0 || index >= m_cores.size()) {
        return;
    }

    auto &core = m_cores[index];
    core.usage = usage;
    core.history.append(usage);
    while (core.history.size() > kMaxHistory) {
        core.history.removeFirst();
    }
}

void CpuModel::updateProcessStats(const QVector<ProcessSample> &processes, qint64 totalDiff)
{
    const int processCount = processes.size();
    const bool processCountChanged = processCount != m_processCount;
    m_processCount = processCount;

    const int runningProcesses = readRunningProcesses();
    const bool runningChanged = runningProcesses != m_runningProcesses;
    m_runningProcesses = runningProcesses;

    QVariantList topProcesses;
    QVariantList topMemoryProcesses;
    struct RankedProcess {
        int pid = 0;
        QString name;
        qint64 delta = 0;
        int usage = 0;
        qint64 rssKb = 0;
    };

    QVector<RankedProcess> ranked;
    ranked.reserve(processes.size());
    for (const ProcessSample &process : processes) {
        const qint64 previousTicks = m_lastProcessTicks.value(process.pid, process.ticks);
        const qint64 delta = qMax<qint64>(0, process.ticks - previousTicks);
        m_lastProcessTicks.insert(process.pid, process.ticks);

        const int usage = totalDiff > 0
            ? qBound(0, static_cast<int>((delta * 100.0 * qMax(1, m_cores.size())) / totalDiff), 10000)
            : 0;
        ranked.append(RankedProcess{process.pid, process.name, delta, usage, process.rssKb});
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedProcess &a, const RankedProcess &b) {
        if (a.delta == b.delta) {
            return a.pid < b.pid;
        }
        return a.delta > b.delta;
    });

    const int topCount = qMin(3, ranked.size());
    topProcesses.reserve(topCount);
    for (int i = 0; i < topCount; ++i) {
        const RankedProcess &process = ranked.at(i);
        QVariantMap item;
        item.insert(QStringLiteral("pid"), process.pid);
        item.insert(QStringLiteral("name"), process.name);
        item.insert(QStringLiteral("usage"), process.usage / 100.0);
        topProcesses.append(item);
    }

    QVector<RankedProcess> memoryRanked = ranked;
    std::sort(memoryRanked.begin(), memoryRanked.end(), [](const RankedProcess &a, const RankedProcess &b) {
        if (a.rssKb == b.rssKb) {
            return a.pid < b.pid;
        }
        return a.rssKb > b.rssKb;
    });

    const int topMemoryCount = qMin(3, memoryRanked.size());
    topMemoryProcesses.reserve(topMemoryCount);
    for (int i = 0; i < topMemoryCount; ++i) {
        const RankedProcess &process = memoryRanked.at(i);
        // Ranking stays on the cheap RSS (read for every process during sampling),
        // but we DISPLAY the honest PSS for the handful actually shown — only a few
        // smaps_rollup page-table walks per tick. Fall back to RSS where PSS is
        // unavailable (old kernel / another user's process).
        const qint64 pssKb = readProcessPssKb(process.pid);
        const qint64 memKb = pssKb > 0 ? pssKb : process.rssKb;
        QVariantMap item;
        item.insert(QStringLiteral("pid"), process.pid);
        item.insert(QStringLiteral("name"), process.name);
        item.insert(QStringLiteral("memoryKb"), memKb);
        item.insert(QStringLiteral("memoryMiB"), memKb / 1024.0);
        item.insert(QStringLiteral("usage"), memKb > 0 ? (memKb / 1024.0) : 0.0);
        topMemoryProcesses.append(item);
    }

    if (processCountChanged || runningChanged || topProcesses != m_topProcesses || topMemoryProcesses != m_topMemoryProcesses) {
        m_topProcesses = topProcesses;
        m_topMemoryProcesses = topMemoryProcesses;
        emit processStatsChanged();
    } else {
        m_topProcesses = topProcesses;
        m_topMemoryProcesses = topMemoryProcesses;
    }
}

void CpuModel::resetSamples(const QVector<Sample> &samples)
{
    m_cores.clear();
    m_cores.reserve(samples.size() > 0 ? samples.size() - 1 : 0);
    m_usageHistory.clear();
    m_loadAverageHistory.clear();
    m_memoryUsageHistory.clear();
    m_swapUsageHistory.clear();

    for (int i = 1; i < samples.size(); ++i) {
        const Sample &sample = samples.at(i);
        CoreState state;
        state.name = sample.name;
        state.total = sample.total;
        state.idle = sample.idle;
        state.usage = 0;
        state.valid = true;
        state.history.append(0);
        m_cores.append(state);
    }

    m_usageHistory.append(0);
    emit usageHistoryChanged();
    m_loadAverageHistory.append(0.0);
    emit loadAverageHistoryChanged();
    m_memoryUsageHistory.append(0);
    m_swapUsageHistory.append(0);
    emit memoryStatsChanged();

    emit coresChanged();
}

void CpuModel::refresh()
{
    const LoadAverageSample loadAverage = readLoadAverage();
    if (loadAverage.valid) {
        const bool changed = !qFuzzyCompare(m_loadAverage1 + 1.0, loadAverage.one + 1.0)
            || !qFuzzyCompare(m_loadAverage5 + 1.0, loadAverage.five + 1.0)
            || !qFuzzyCompare(m_loadAverage15 + 1.0, loadAverage.fifteen + 1.0);
        m_loadAverage1 = loadAverage.one;
        m_loadAverage5 = loadAverage.five;
        m_loadAverage15 = loadAverage.fifteen;
        appendLoadAverage(loadAverage.one);
        if (changed) {
            emit loadAverageChanged();
        }
    }

    const double clock = readClockMhz();
    if (!qFuzzyCompare(m_clockMhz + 1.0, clock + 1.0)) {
        m_clockMhz = clock;
        emit clockChanged();
    }

    const QVector<ProcessSample> processes = readProcessSamples();
    qint64 memUsedKb = 0;
    qint64 memTotalKb = 0;
    const int memoryUsage = readMemoryUsage(&memUsedKb, &memTotalKb);
    if (memUsedKb != m_memoryUsedKb || memTotalKb != m_memoryTotalKb) {
        m_memoryUsedKb = memUsedKb;
        m_memoryTotalKb = memTotalKb;
        emit memoryStatsChanged();
    }
    const int swapUsage = readSwapUsage();
    const QVector<Sample> samples = readSamples();

    if (samples.isEmpty()) {
        if (memoryUsage != m_memoryUsage) {
            m_memoryUsage = memoryUsage;
            appendMemoryUsage(memoryUsage);
        }
        if (swapUsage != m_swapUsage) {
            m_swapUsage = swapUsage;
            appendSwapUsage(swapUsage);
        }
        updateProcessStats(processes, 0);
        return;
    }

    qint64 totalDiff = 0;
    if (!m_hasSample || m_cores.size() != samples.size() - 1) {
        m_lastSample = samples.first();
        m_hasSample = true;
        m_usage = 0;
        resetSamples(samples);
        emit usageChanged();
    } else {
        const Sample overall = samples.first();
        totalDiff = overall.total - m_lastSample.total;
        const qint64 idleDiff = overall.idle - m_lastSample.idle;
        m_lastSample = overall;

        if (totalDiff > 0) {
            const int newUsage = qBound(0, static_cast<int>((((totalDiff - idleDiff) * 100.0) / totalDiff)), 100);
            if (newUsage != m_usage) {
                m_usage = newUsage;
                emit usageChanged();
            }
            appendUsage(newUsage);
        }

        bool anyCoreChanged = false;
        for (int i = 1; i < samples.size() && (i - 1) < m_cores.size(); ++i) {
            const Sample &sample = samples.at(i);
            auto &core = m_cores[i - 1];
            const qint64 coreTotalDiff = sample.total - core.total;
            const qint64 coreIdleDiff = sample.idle - core.idle;
            core.total = sample.total;
            core.idle = sample.idle;

            if (coreTotalDiff <= 0) {
                continue;
            }

            const int newCoreUsage = qBound(0, static_cast<int>((((coreTotalDiff - coreIdleDiff) * 100.0) / coreTotalDiff)), 100);
            if (newCoreUsage != core.usage) {
                core.usage = newCoreUsage;
                anyCoreChanged = true;
            }
            core.history.append(newCoreUsage);
            while (core.history.size() > kMaxHistory) {
                core.history.removeFirst();
            }
            anyCoreChanged = true;
        }

        if (anyCoreChanged) {
            emit coresChanged();
        }
    }

    if (memoryUsage != m_memoryUsage) {
        m_memoryUsage = memoryUsage;
        appendMemoryUsage(memoryUsage);
    }

    if (swapUsage != m_swapUsage) {
        m_swapUsage = swapUsage;
        appendSwapUsage(swapUsage);
    }

    updateProcessStats(processes, totalDiff);
}
