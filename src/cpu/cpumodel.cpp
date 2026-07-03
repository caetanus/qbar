#include "cpumodel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QIODevice>
#include <QtGlobal>
#include <QVariantMap>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

constexpr int kMaxHistory = 24;
// Cheap tick: /proc/stat + /proc/loadavg + /proc/meminfo (three small files) — drives
// the bar graphs; unchanged 5s cadence.
constexpr int kGlobalIntervalMs = 5000;
// Per-process scan cadence with a details popup open…
constexpr int kDetailsOpenIntervalMs = 5000;
// …and the always-on idle heartbeat with it closed. Deliberately NOT lazy: an
// incident's forensics need a warm tick baseline so the popup answers instantly with
// the average over the last window — including the seconds BEFORE it was opened.
constexpr int kDetailsIdleIntervalMs = 12000;

// All /proc parsing below sticks to QByteArray: these files are ASCII and the QString
// fromUtf8+split round-trip was the bulk of the old per-tick cost.
QByteArray readAll(const char *path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// Hot-path variant for the per-PID scan: raw open/read into a stack buffer. With
// thousands of tiny files per sweep, QFile's construction/locking overhead (~tens of
// µs each) was costing more than the syscalls themselves. Returns the byte count,
// or -1 when the file vanished (process exited mid-sweep — normal, skip it).
int readSmall(const char *path, char *buf, int cap)
{
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    int total = 0;
    for (;;) {
        const ssize_t n = ::read(fd, buf + total, static_cast<size_t>(cap - total));
        if (n <= 0 || total + n >= cap) {
            total += n > 0 ? static_cast<int>(n) : 0;
            break;
        }
        total += static_cast<int>(n);
    }
    ::close(fd);
    return total;
}

// Advance past `count` space-separated tokens starting at p (NUL-terminated).
const char *skipTokens(const char *p, int count)
{
    while (count > 0 && *p != '\0') {
        while (*p == ' ') ++p;
        while (*p != '\0' && *p != ' ') ++p;
        --count;
    }
    while (*p == ' ') ++p;
    return p;
}

} // namespace

CpuModel::CpuModel(QObject *parent)
    : QObject(parent)
{
    refreshGlobal();
    QTimer::singleShot(1000, this, &CpuModel::refreshGlobal);
    m_timer.setInterval(kGlobalIntervalMs);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &CpuModel::refreshGlobal);
    m_timer.start();

    // Seed the per-process tick baseline right away (deltas start meaningful one
    // heartbeat later, same as the old first-refresh behaviour).
    refreshDetails();
    m_detailsTimer.setInterval(kDetailsIdleIntervalMs);
    m_detailsTimer.setSingleShot(false);
    connect(&m_detailsTimer, &QTimer::timeout, this, &CpuModel::refreshDetails);
    m_detailsTimer.start();
}

void CpuModel::acquireDetails()
{
    ++m_detailsRefs;
    if (m_detailsRefs == 1) {
        m_detailsTimer.setInterval(kDetailsOpenIntervalMs);
        // Paint the popup with fresh numbers immediately: the delta against the idle
        // heartbeat's baseline (≤ one heartbeat old) is exactly the recent-window
        // average an incident investigation wants.
        refreshDetails();
    }
}

void CpuModel::releaseDetails()
{
    m_detailsRefs = qMax(0, m_detailsRefs - 1);
    if (m_detailsRefs == 0) {
        m_detailsTimer.setInterval(kDetailsIdleIntervalMs);
    }
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

qint64 CpuModel::memoryFreeBytes() const
{
    return m_memoryFreeKb * 1024;
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

QVector<CpuModel::Sample> CpuModel::readSamples(int *procsRunning) const
{
    const QByteArray content = readAll("/proc/stat");
    if (content.isEmpty()) {
        return {};
    }

    QVector<Sample> samples;
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &rawLine : lines) {
        if (rawLine.startsWith("procs_running")) {
            if (procsRunning != nullptr) {
                bool ok = false;
                const int running = rawLine.mid(13).trimmed().toInt(&ok);
                if (ok) {
                    *procsRunning = running;
                }
            }
            continue;
        }
        if (!rawLine.startsWith("cpu")) {
            continue;
        }

        const QList<QByteArray> parts = rawLine.simplified().split(' ');
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

        samples.append(Sample{QString::fromLatin1(parts.first()), total, idle, true});
    }

    return samples;
}

CpuModel::LoadAverageSample CpuModel::readLoadAverage() const
{
    const QByteArray line = readAll("/proc/loadavg");
    const QList<QByteArray> parts = line.simplified().split(' ');
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
    // The hot path of the details scan: three tiny per-PID files, read with raw
    // syscalls into stack buffers. stat gives the tick counter, comm the name, and
    // statm the resident pages — statm is a single short line, unlike the old full
    // /proc/N/status scan for VmRSS (same number, ~30 lines cheaper).
    static const qint64 pageKb = sysconf(_SC_PAGESIZE) / 1024;

    QVector<ProcessSample> processes;
    processes.reserve(m_lastProcessTicks.size() + 64);

    DIR *proc = ::opendir("/proc");
    if (proc == nullptr) {
        return processes;
    }

    char path[64];
    char buf[1024];

    while (const dirent *entry = ::readdir(proc)) {
        const char *name = entry->d_name;
        if (name[0] < '0' || name[0] > '9') {
            continue;
        }
        char *end = nullptr;
        const long pid = std::strtol(name, &end, 10);
        if (end == nullptr || *end != '\0' || pid <= 0) {
            continue;
        }

        // /proc/N/stat: "pid (comm) S ppid ..." — utime/stime are the 12th/13th
        // fields after the closing paren (comm may contain spaces, hence the paren
        // scan instead of a plain tokenizer from the start).
        qsnprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        int len = readSmall(path, buf, sizeof(buf) - 1);
        if (len <= 0) {
            continue;
        }
        buf[len] = '\0';
        const char *closeParen = ::strrchr(buf, ')');
        if (closeParen == nullptr || closeParen[1] == '\0') {
            continue;
        }
        const char *utimePos = skipTokens(closeParen + 2, 11);
        char *afterUtime = nullptr;
        const qint64 utime = std::strtoll(utimePos, &afterUtime, 10);
        if (afterUtime == utimePos) {
            continue;
        }
        char *afterStime = nullptr;
        const qint64 stime = std::strtoll(afterUtime, &afterStime, 10);
        if (afterStime == afterUtime) {
            continue;
        }

        // comm changes only on exec — cache it per PID and pay the read once. The
        // cache is pruned against live PIDs after every sweep (see refreshDetails).
        QString procName = m_processNames.value(static_cast<int>(pid));
        if (procName.isEmpty()) {
            qsnprintf(path, sizeof(path), "/proc/%ld/comm", pid);
            len = readSmall(path, buf, sizeof(buf) - 1);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
                --len;
            }
            procName = len > 0 ? QString::fromUtf8(buf, len)
                               : QStringLiteral("pid %1").arg(pid);
            m_processNames.insert(static_cast<int>(pid), procName);
        }

        // /proc/N/statm: "size resident shared ..." (pages). RSS is instantaneous
        // (no delta/baseline needed), so the idle heartbeat skips it entirely — the
        // synchronous refresh in acquireDetails() fills it in before the popup's
        // first frame.
        qint64 rssKb = 0;
        if (m_detailsRefs > 0) {
            qsnprintf(path, sizeof(path), "/proc/%ld/statm", pid);
            len = readSmall(path, buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                const char *residentPos = skipTokens(buf, 1);
                rssKb = qMax<qint64>(0, std::strtoll(residentPos, nullptr, 10)) * pageKb;
            }
        }

        processes.append(ProcessSample{static_cast<int>(pid), procName, utime + stime, rssKb, true});
    }

    ::closedir(proc);
    return processes;
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

int CpuModel::readMemInfo(qint64 *usedKb, qint64 *totalKb, qint64 *freeKb, int *swapUsage) const
{
    const QByteArray content = readAll("/proc/meminfo");
    if (content.isEmpty()) {
        return 0;
    }

    qint64 memTotal = 0;
    qint64 memAvailable = -1;
    qint64 memFree = 0;
    qint64 buffers = 0;
    qint64 cached = 0;
    qint64 swapTotal = 0;
    qint64 swapFree = 0;

    auto valueFor = [](const QByteArray &line) -> qint64 {
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 2) {
            return 0;
        }
        bool ok = false;
        const qint64 value = parts.at(1).toLongLong(&ok);
        return ok ? value : 0;
    };

    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("MemTotal:")) {
            memTotal = valueFor(line);
        } else if (line.startsWith("MemAvailable:")) {
            memAvailable = valueFor(line);
        } else if (line.startsWith("MemFree:")) {
            memFree = valueFor(line);
        } else if (line.startsWith("Buffers:")) {
            buffers = valueFor(line);
        } else if (line.startsWith("Cached:")) {
            cached = valueFor(line);
        } else if (line.startsWith("SwapTotal:")) {
            swapTotal = valueFor(line);
        } else if (line.startsWith("SwapFree:")) {
            swapFree = valueFor(line);
        }
    }

    if (swapUsage != nullptr) {
        const qint64 swapUsed = qMax<qint64>(0, swapTotal - swapFree);
        *swapUsage = swapTotal > 0
            ? qBound(0, static_cast<int>((swapUsed * 100.0) / swapTotal), 100)
            : 0;
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
    if (freeKb != nullptr) {
        // Truly free (MemFree), not "available": the gap between free and available
        // is reclaimable cache/buffers, which the popup paints as its own segment.
        *freeKb = qBound<qint64>(0, memFree, memTotal);
    }
    return qBound(0, static_cast<int>((used * 100.0) / memTotal), 100);
}

double CpuModel::readClockMhz() const
{
    // Prefer sysfs: one tiny scaling_cur_freq file per core (kHz) instead of the old
    // full /proc/cpuinfo parse — cpuinfo grows with the flags lines on every core.
    double sum = 0.0;
    int count = 0;
    QDirIterator it(QStringLiteral("/sys/devices/system/cpu"),
                    {QStringLiteral("cpu[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString base = it.next();
        QFile f(base + QStringLiteral("/cpufreq/scaling_cur_freq"));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        bool ok = false;
        const qint64 khz = f.readAll().trimmed().toLongLong(&ok);
        if (ok && khz > 0) {
            sum += khz / 1000.0;
            count += 1;
        }
    }
    if (count > 0) {
        return sum / count;
    }

    // Fallback (no cpufreq, e.g. some VMs): the "cpu MHz" lines from /proc/cpuinfo.
    const QByteArray content = readAll("/proc/cpuinfo");
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("cpu MHz")) {
            continue;
        }
        const int colon = line.indexOf(':');
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

    if (processCountChanged || topProcesses != m_topProcesses || topMemoryProcesses != m_topMemoryProcesses) {
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

void CpuModel::refreshGlobal()
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

    qint64 memUsedKb = 0;
    qint64 memTotalKb = 0;
    qint64 memFreeKb = 0;
    int swapUsage = 0;
    const int memoryUsage = readMemInfo(&memUsedKb, &memTotalKb, &memFreeKb, &swapUsage);
    if (memUsedKb != m_memoryUsedKb || memTotalKb != m_memoryTotalKb || memFreeKb != m_memoryFreeKb) {
        m_memoryUsedKb = memUsedKb;
        m_memoryTotalKb = memTotalKb;
        m_memoryFreeKb = memFreeKb;
        emit memoryStatsChanged();
    }

    int procsRunning = m_runningProcesses;
    const QVector<Sample> samples = readSamples(&procsRunning);
    if (procsRunning != m_runningProcesses) {
        m_runningProcesses = procsRunning;
        emit processStatsChanged();
    }

    if (samples.isEmpty()) {
        if (memoryUsage != m_memoryUsage) {
            m_memoryUsage = memoryUsage;
            appendMemoryUsage(memoryUsage);
        }
        if (swapUsage != m_swapUsage) {
            m_swapUsage = swapUsage;
            appendSwapUsage(swapUsage);
        }
        return;
    }

    if (!m_hasSample || m_cores.size() != samples.size() - 1) {
        m_lastSample = samples.first();
        m_hasSample = true;
        m_usage = 0;
        resetSamples(samples);
        emit usageChanged();
    } else {
        const Sample overall = samples.first();
        const qint64 totalDiff = overall.total - m_lastSample.total;
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
}

void CpuModel::refreshDetails()
{
    const double clock = readClockMhz();
    if (!qFuzzyCompare(m_clockMhz + 1.0, clock + 1.0)) {
        m_clockMhz = clock;
        emit clockChanged();
    }

    const QVector<ProcessSample> processes = readProcessSamples();

    // Normalize the per-process tick deltas over the DETAILS window (last details
    // scan → now), reading the overall counter fresh so a scan triggered by
    // acquireDetails() doesn't reuse a global sample up to one global tick old.
    const QVector<Sample> samples = readSamples();
    const qint64 totalNow = samples.isEmpty() ? m_lastSample.total : samples.first().total;
    const qint64 totalDiff = m_lastDetailsCpuTotal > 0
        ? qMax<qint64>(0, totalNow - m_lastDetailsCpuTotal)
        : 0;
    m_lastDetailsCpuTotal = totalNow;

    updateProcessStats(processes, totalDiff);

    // Prune bookkeeping for PIDs that died — both hashes would otherwise grow for
    // the lifetime of the bar.
    QHash<int, qint64> liveTicks;
    QHash<int, QString> liveNames;
    liveTicks.reserve(processes.size());
    liveNames.reserve(processes.size());
    for (const ProcessSample &process : processes) {
        const auto ticksIt = m_lastProcessTicks.constFind(process.pid);
        if (ticksIt != m_lastProcessTicks.constEnd()) {
            liveTicks.insert(process.pid, ticksIt.value());
        }
        const auto nameIt = m_processNames.constFind(process.pid);
        if (nameIt != m_processNames.constEnd()) {
            liveNames.insert(process.pid, nameIt.value());
        }
    }
    m_lastProcessTicks = std::move(liveTicks);
    m_processNames = std::move(liveNames);
}
