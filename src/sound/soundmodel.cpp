#include "soundmodel.h"

#include <algorithm>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace {

constexpr int kRefreshIntervalMs = 1000;

} // namespace

SoundModel::SoundModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    QTimer::singleShot(1000, this, &SoundModel::refresh);
    m_timer.setInterval(kRefreshIntervalMs);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &SoundModel::refresh);
    m_timer.start();
}

bool SoundModel::available() const
{
    return m_available;
}

bool SoundModel::muted() const
{
    return m_muted;
}

int SoundModel::volume() const
{
    return m_volume;
}

QString SoundModel::sinkName() const
{
    return m_sinkName;
}

QString SoundModel::displayText() const
{
    if (!m_available || m_volume < 0) {
        return QStringLiteral("--");
    }

    return QStringLiteral("%1%").arg(m_volume);
}

QString SoundModel::tooltipText() const
{
    QStringList parts;
    if (m_available) {
        parts << friendlySinkName(m_sinkName);
        if (m_muted) {
            parts << QStringLiteral("muted");
        }
        parts << QStringLiteral("%1%").arg(m_volume);
    } else {
        parts << QStringLiteral("sound unavailable");
    }
    return parts.join(QStringLiteral(" | "));
}

QString SoundModel::outputTooltipText() const
{
    if (!m_available) {
        return QStringLiteral("sound unavailable");
    }

    const QStringList parts = m_muted
        ? QStringList{friendlySinkName(m_sinkName), QStringLiteral("muted"), QStringLiteral("%1%").arg(m_volume)}
        : QStringList{friendlySinkName(m_sinkName), QStringLiteral("%1%").arg(m_volume)};
    return parts.join(QStringLiteral(" | "));
}

bool SoundModel::sourceAvailable() const
{
    return m_sourceAvailable;
}

bool SoundModel::sourceMuted() const
{
    return m_sourceMuted;
}

int SoundModel::sourceVolume() const
{
    return m_sourceVolume;
}

QString SoundModel::sourceName() const
{
    return m_sourceName;
}

QString SoundModel::sourceDisplayText() const
{
    if (!m_sourceAvailable || m_sourceVolume < 0) {
        return QStringLiteral("--");
    }

    return QStringLiteral("%1%").arg(m_sourceVolume);
}

QString SoundModel::sourceTooltipText() const
{
    if (!m_sourceAvailable) {
        return QStringLiteral("mic unavailable");
    }

    const QStringList parts = m_sourceMuted
        ? QStringList{friendlySourceName(m_sourceName), QStringLiteral("muted"), QStringLiteral("%1%").arg(m_sourceVolume)}
        : QStringList{friendlySourceName(m_sourceName), QStringLiteral("%1%").arg(m_sourceVolume)};
    return parts.join(QStringLiteral(" | "));
}

QString SoundModel::runPactl(const QStringList &args, bool *ok)
{
    QProcess process;
    process.start(QStringLiteral("pactl"), args);
    if (!process.waitForFinished(500)) {
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }

    const bool success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok != nullptr) {
        *ok = success;
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString SoundModel::defaultSinkName()
{
    bool ok = false;
    const QString info = runPactl({QStringLiteral("info")}, &ok);
    if (!ok || info.isEmpty()) {
        return {};
    }

    const QStringList lines = info.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("Default Sink:"))) {
            continue;
        }

        const QString sink = line.mid(QStringLiteral("Default Sink:").size()).trimmed();
        return sink;
    }

    return {};
}

QString SoundModel::defaultSourceName()
{
    bool ok = false;
    const QString info = runPactl({QStringLiteral("info")}, &ok);
    if (!ok || info.isEmpty()) {
        return {};
    }

    const QStringList lines = info.split(QChar::LineFeed, Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("Default Source:"))) {
            continue;
        }

        return line.mid(QStringLiteral("Default Source:").size()).trimmed();
    }

    return {};
}

QString SoundModel::friendlySinkName(const QString &sinkName)
{
    if (sinkName.isEmpty()) {
        return QStringLiteral("default sink");
    }

    bool ok = false;
    const QString output = runPactl({QStringLiteral("list"), QStringLiteral("sinks")}, &ok);
    if (!ok || output.isEmpty()) {
        return sinkName;
    }

    const QStringList lines = output.split(QChar::LineFeed, Qt::SkipEmptyParts);
    QString currentName;
    QString currentDescription;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Name:"))) {
            currentName = line.mid(QStringLiteral("Name:").size()).trimmed();
            currentDescription.clear();
            continue;
        }
        if (line.startsWith(QStringLiteral("Description:"))) {
            currentDescription = line.mid(QStringLiteral("Description:").size()).trimmed();
        }
        if (currentName == sinkName && !currentDescription.isEmpty()) {
            return currentDescription;
        }
    }

    return sinkName;
}

QString SoundModel::friendlySourceName(const QString &sourceName)
{
    if (sourceName.isEmpty()) {
        return QStringLiteral("default source");
    }

    bool ok = false;
    const QString output = runPactl({QStringLiteral("list"), QStringLiteral("sources")}, &ok);
    if (!ok || output.isEmpty()) {
        return sourceName;
    }

    const QStringList lines = output.split(QChar::LineFeed, Qt::SkipEmptyParts);
    QString currentName;
    QString currentDescription;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Name:"))) {
            currentName = line.mid(QStringLiteral("Name:").size()).trimmed();
            currentDescription.clear();
            continue;
        }
        if (line.startsWith(QStringLiteral("Description:"))) {
            currentDescription = line.mid(QStringLiteral("Description:").size()).trimmed();
        }
        if (currentName == sourceName && !currentDescription.isEmpty()) {
            return currentDescription;
        }
    }

    return sourceName;
}

int SoundModel::parseVolumePercent(const QString &output)
{
    const QRegularExpression regex(QStringLiteral(R"((\d+)%)"));
    QRegularExpressionMatchIterator it = regex.globalMatch(output);
    int count = 0;
    int total = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (!ok) {
            continue;
        }
        total += value;
        ++count;
    }

    if (count <= 0) {
        return -1;
    }

    return qBound(0, qRound(total / static_cast<double>(count)), 150);
}

bool SoundModel::parseMuted(const QString &output)
{
    return output.contains(QStringLiteral("yes"), Qt::CaseInsensitive);
}

void SoundModel::refresh()
{
    const QString sink = defaultSinkName();
    const QString source = defaultSourceName();

    State sinkState;
    if (!sink.isEmpty()) {
        bool volumeOk = false;
        const QString volumeOutput = runPactl({QStringLiteral("get-sink-volume"), sink}, &volumeOk);
        bool muteOk = false;
        const QString muteOutput = runPactl({QStringLiteral("get-sink-mute"), sink}, &muteOk);
        const int volume = parseVolumePercent(volumeOutput);
        const bool muted = muteOk ? parseMuted(muteOutput) : m_muted;
        if (volumeOk && volume >= 0) {
            sinkState.sinkName = sink;
            sinkState.volume = qBound(0, volume, 100);
            sinkState.muted = muted;
            sinkState.available = true;
        }
    }

    State sourceState;
    if (!source.isEmpty()) {
        bool volumeOk = false;
        const QString volumeOutput = runPactl({QStringLiteral("get-source-volume"), source}, &volumeOk);
        bool muteOk = false;
        const QString muteOutput = runPactl({QStringLiteral("get-source-mute"), source}, &muteOk);
        const int volume = parseVolumePercent(volumeOutput);
        const bool muted = muteOk ? parseMuted(muteOutput) : m_sourceMuted;
        if (volumeOk && volume >= 0) {
            sourceState.sinkName = source;
            sourceState.volume = qBound(0, volume, 100);
            sourceState.muted = muted;
            sourceState.available = true;
        }
    }

    if (!sinkState.available && !sourceState.available) {
        if (!m_available && !m_sourceAvailable) {
            return;
        }
        setState(State{});
        setSourceState(State{});
        return;
    }

    setState(sinkState);
    setSourceState(sourceState);
}

void SoundModel::setState(const State &state)
{
    const bool availabilityChangedLocal = m_available != state.available;
    const bool dataChanged = m_sinkName != state.sinkName
        || m_volume != state.volume
        || m_muted != state.muted
        || m_available != state.available;

    m_sinkName = state.sinkName;
    m_volume = state.volume;
    m_muted = state.muted;
    m_available = state.available;

    if (availabilityChangedLocal) {
        emit availabilityChanged();
    }
    if (dataChanged) {
        emit volumeChanged();
    }
}

void SoundModel::setSourceState(const State &state)
{
    const bool availabilityChangedLocal = m_sourceAvailable != state.available;
    const bool dataChanged = m_sourceName != state.sinkName
        || m_sourceVolume != state.volume
        || m_sourceMuted != state.muted
        || m_sourceAvailable != state.available;

    m_sourceName = state.sinkName;
    m_sourceVolume = state.volume;
    m_sourceMuted = state.muted;
    m_sourceAvailable = state.available;

    if (availabilityChangedLocal) {
        emit availabilityChanged();
    }
    if (dataChanged) {
        emit volumeChanged();
    }
}

void SoundModel::adjustPercent(int deltaPercent)
{
    if (!m_available || m_sinkName.isEmpty()) {
        return;
    }

    const int target = qBound(0, m_volume + deltaPercent, 150);
    bool ok = false;
    runPactl({QStringLiteral("set-sink-volume"), m_sinkName, QStringLiteral("%1%").arg(target)}, &ok);
    if (ok) {
        refresh();
    }
}

void SoundModel::stepUp(int percent)
{
    adjustPercent(std::max(1, percent));
}

void SoundModel::stepDown(int percent)
{
    adjustPercent(-std::max(1, percent));
}

void SoundModel::setPercent(int percent)
{
    if (!m_available || m_sinkName.isEmpty()) {
        return;
    }

    const int target = qBound(0, percent, 150);
    bool ok = false;
    runPactl({QStringLiteral("set-sink-volume"), m_sinkName, QStringLiteral("%1%").arg(target)}, &ok);
    if (ok) {
        refresh();
    }
}

void SoundModel::toggleMute()
{
    if (!m_available || m_sinkName.isEmpty()) {
        return;
    }

    bool ok = false;
    runPactl({QStringLiteral("set-sink-mute"), m_sinkName, QStringLiteral("toggle")}, &ok);
    if (ok) {
        refresh();
    }
}

void SoundModel::adjustSourcePercent(int deltaPercent)
{
    if (!m_sourceAvailable || m_sourceName.isEmpty()) {
        return;
    }

    const int target = qBound(0, m_sourceVolume + deltaPercent, 150);
    bool ok = false;
    runPactl({QStringLiteral("set-source-volume"), m_sourceName, QStringLiteral("%1%").arg(target)}, &ok);
    if (ok) {
        refresh();
    }
}

void SoundModel::stepSourceUp(int percent)
{
    adjustSourcePercent(std::max(1, percent));
}

void SoundModel::stepSourceDown(int percent)
{
    adjustSourcePercent(-std::max(1, percent));
}

void SoundModel::setSourcePercent(int percent)
{
    if (!m_sourceAvailable || m_sourceName.isEmpty()) {
        return;
    }

    const int target = qBound(0, percent, 150);
    bool ok = false;
    runPactl({QStringLiteral("set-source-volume"), m_sourceName, QStringLiteral("%1%").arg(target)}, &ok);
    if (ok) {
        refresh();
    }
}

void SoundModel::toggleSourceMute()
{
    if (!m_sourceAvailable || m_sourceName.isEmpty()) {
        return;
    }

    bool ok = false;
    runPactl({QStringLiteral("set-source-mute"), m_sourceName, QStringLiteral("toggle")}, &ok);
    if (ok) {
        refresh();
    }
}
