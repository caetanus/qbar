#include "temperaturemodel.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <limits>

namespace {

constexpr int kRefreshIntervalMs = 5000;

} // namespace

TemperatureModel::TemperatureModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    QTimer::singleShot(1000, this, &TemperatureModel::refresh);
    m_timer.setInterval(kRefreshIntervalMs);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &TemperatureModel::refresh);
    m_timer.start();
}

int TemperatureModel::cpuTemperature() const
{
    return m_cpuTemperature;
}

int TemperatureModel::gpuTemperature() const
{
    return m_gpuTemperature;
}

bool TemperatureModel::cpuAvailable() const
{
    return m_cpuAvailable;
}

bool TemperatureModel::gpuAvailable() const
{
    return m_gpuAvailable;
}

bool TemperatureModel::available() const
{
    return m_cpuAvailable || m_gpuAvailable;
}

QString TemperatureModel::displayText() const
{
    if (m_cpuAvailable && m_gpuAvailable) {
        return QStringLiteral("%1°/%2°").arg(m_cpuTemperature).arg(m_gpuTemperature);
    }
    if (m_cpuAvailable) {
        return QStringLiteral("%1°").arg(m_cpuTemperature);
    }
    if (m_gpuAvailable) {
        return QStringLiteral("%1°").arg(m_gpuTemperature);
    }
    return QString();
}

QString TemperatureModel::tooltipText() const
{
    QStringList parts;
    if (m_cpuAvailable) {
        parts << QStringLiteral("CPU %1°C (%2)").arg(m_cpuTemperature).arg(m_cpuName);
    } else {
        parts << QStringLiteral("CPU unavailable");
    }
    if (m_gpuAvailable) {
        parts << QStringLiteral("GPU %1°C (%2)").arg(m_gpuTemperature).arg(m_gpuName);
    } else {
        parts << QStringLiteral("GPU unavailable");
    }
    return parts.join(QStringLiteral(" | "));
}

QString TemperatureModel::readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromUtf8(file.readAll()).trimmed();
}

int TemperatureModel::scoreCpuSensor(const QString &deviceName, const QString &sensorLabel)
{
    const QString device = deviceName.toLower();
    const QString label = sensorLabel.toLower();
    int score = 0;

    if (device.contains(QStringLiteral("coretemp"))) score += 120;
    if (device.contains(QStringLiteral("k10temp"))) score += 120;
    if (device.contains(QStringLiteral("zenpower"))) score += 120;
    if (device.contains(QStringLiteral("cpu"))) score += 80;
    if (device.contains(QStringLiteral("acpitz"))) score += 35;
    if (device.contains(QStringLiteral("thermal"))) score += 20;

    if (label.contains(QStringLiteral("package"))) score += 60;
    if (label.contains(QStringLiteral("tctl")) || label.contains(QStringLiteral("tdie"))) score += 55;
    if (label.contains(QStringLiteral("cpu"))) score += 30;
    if (label.contains(QStringLiteral("core"))) score += 15;
    if (label.contains(QStringLiteral("gpu"))) score -= 120;

    return score;
}

int TemperatureModel::scoreGpuSensor(const QString &deviceName, const QString &sensorLabel)
{
    const QString device = deviceName.toLower();
    const QString label = sensorLabel.toLower();
    int score = 0;

    if (device.contains(QStringLiteral("amdgpu"))) score += 120;
    if (device.contains(QStringLiteral("nouveau"))) score += 120;
    if (device.contains(QStringLiteral("nvidia"))) score += 120;
    if (device.contains(QStringLiteral("radeon"))) score += 110;
    if (device.contains(QStringLiteral("i915"))) score += 90;
    if (device.contains(QStringLiteral("gpu"))) score += 100;

    if (label.contains(QStringLiteral("gpu"))) score += 60;
    if (label.contains(QStringLiteral("edge"))) score += 25;
    if (label.contains(QStringLiteral("junction"))) score += 25;
    if (label.contains(QStringLiteral("hotspot"))) score += 25;
    if (label.contains(QStringLiteral("mem"))) score += 10;
    if (label.contains(QStringLiteral("package")) || label.contains(QStringLiteral("core")) || label.contains(QStringLiteral("cpu"))) {
        score -= 80;
    }

    return score;
}

QString TemperatureModel::readingName(const SensorReading &reading)
{
    if (!reading.sensorLabel.isEmpty()) {
        return reading.sensorLabel;
    }
    return reading.deviceName;
}

QVector<TemperatureModel::SensorReading> TemperatureModel::readSensorReadings() const
{
    QVector<SensorReading> readings;
    const QDir hwmonRoot(QStringLiteral("/sys/class/hwmon"));
    const QStringList hwmons = hwmonRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &hwmon : hwmons) {
        const QString hwmonPath = hwmonRoot.filePath(hwmon);
        const QString deviceName = readTextFile(hwmonPath + QStringLiteral("/name"));
        const QDir hwmonDir(hwmonPath);
        const QStringList inputFiles = hwmonDir.entryList(QStringList() << QStringLiteral("temp*_input"), QDir::Files, QDir::Name);
        for (const QString &inputFile : inputFiles) {
            const QString prefix = inputFile.left(inputFile.size() - QStringLiteral("_input").size());
            const QString label = readTextFile(hwmonPath + QLatin1Char('/') + prefix + QStringLiteral("_label"));
            const QString rawValue = readTextFile(hwmonPath + QLatin1Char('/') + inputFile);
            bool ok = false;
            const qint64 milliCelsius = rawValue.toLongLong(&ok);
            if (!ok) {
                continue;
            }

            SensorReading reading;
            reading.deviceName = deviceName.isEmpty() ? hwmon : deviceName;
            reading.sensorLabel = label;
            reading.celsius = qRound(milliCelsius / 1000.0);
            reading.valid = true;
            reading.cpuScore = scoreCpuSensor(reading.deviceName, reading.sensorLabel);
            reading.gpuScore = scoreGpuSensor(reading.deviceName, reading.sensorLabel);
            readings.append(reading);
        }
    }

    return readings;
}

TemperatureModel::SensorReading TemperatureModel::bestReading(const QVector<SensorReading> &readings, bool cpu) const
{
    SensorReading best;
    int bestScore = std::numeric_limits<int>::min();

    for (const SensorReading &reading : readings) {
        if (!reading.valid) {
            continue;
        }
        const int score = cpu ? reading.cpuScore : reading.gpuScore;
        if (score > bestScore) {
            best = reading;
            bestScore = score;
        }
    }

    if (bestScore <= 0) {
        return {};
    }

    return best;
}

void TemperatureModel::refresh()
{
    const QVector<SensorReading> readings = readSensorReadings();
    const SensorReading cpu = bestReading(readings, true);
    const SensorReading gpu = bestReading(readings, false);

    const bool newCpuAvailable = cpu.valid;
    const bool newGpuAvailable = gpu.valid;
    const int newCpuTemperature = newCpuAvailable ? cpu.celsius : 0;
    const int newGpuTemperature = newGpuAvailable ? gpu.celsius : 0;
    const QString newCpuName = newCpuAvailable ? readingName(cpu) : QString();
    const QString newGpuName = newGpuAvailable ? readingName(gpu) : QString();

    const bool changed = newCpuAvailable != m_cpuAvailable
        || newGpuAvailable != m_gpuAvailable
        || newCpuTemperature != m_cpuTemperature
        || newGpuTemperature != m_gpuTemperature
        || newCpuName != m_cpuName
        || newGpuName != m_gpuName;

    if (!changed) {
        return;
    }

    m_cpuAvailable = newCpuAvailable;
    m_gpuAvailable = newGpuAvailable;
    m_cpuTemperature = newCpuTemperature;
    m_gpuTemperature = newGpuTemperature;
    m_cpuName = newCpuName;
    m_gpuName = newGpuName;
    emit temperaturesChanged();
}
