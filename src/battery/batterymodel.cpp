#include "batterymodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTime>

BatteryModel::BatteryModel(QObject *parent)
    : QObject(parent)
{
    m_acpiPath = QStandardPaths::findExecutable(QStringLiteral("acpi"));
    m_devicePath = findBatteryDevice();
    refresh();
    m_timer.setInterval(1000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &BatteryModel::refresh);
    m_timer.start();
}

int BatteryModel::capacity() const
{
    return m_capacity;
}

QString BatteryModel::status() const
{
    return m_status;
}

bool BatteryModel::charging() const
{
    return m_charging;
}

bool BatteryModel::discharging() const
{
    return m_discharging;
}

bool BatteryModel::full() const
{
    return m_full;
}

bool BatteryModel::available() const
{
    return !m_devicePath.isEmpty();
}

bool BatteryModel::healthAvailable() const
{
    return m_healthAvailable;
}

bool BatteryModel::cyclesAvailable() const
{
    return m_cyclesAvailable;
}

bool BatteryModel::energyRateAvailable() const
{
    return m_energyRateAvailable;
}

bool BatteryModel::timeRemainingAvailable() const
{
    return m_timeRemainingAvailable;
}

int BatteryModel::health() const
{
    return m_health;
}

int BatteryModel::cycles() const
{
    return m_cycles;
}

double BatteryModel::energyRate() const
{
    return m_energyRate;
}

int BatteryModel::timeRemaining() const
{
    return m_timeRemaining;
}

void BatteryModel::refresh()
{
    if (m_devicePath.isEmpty()) {
        return;
    }

    auto readFile = [](const QString &path) -> QString {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString::fromUtf8(file.readAll()).trimmed();
        }
        return {};
    };

    const QString capacityStr = readFile(m_devicePath + QStringLiteral("/capacity"));
    const int newCapacity = capacityStr.isEmpty() ? m_capacity : capacityStr.toInt();
    const QString newStatus = readFile(m_devicePath + QStringLiteral("/status"));
    int newHealth = m_health;
    int newTimeRemaining = m_timeRemaining;
    bool newHealthAvailable = QFileInfo::exists(m_devicePath + QStringLiteral("/energy_full"))
        && QFileInfo::exists(m_devicePath + QStringLiteral("/energy_full_design"));
    bool newCyclesAvailable = QFileInfo::exists(m_devicePath + QStringLiteral("/cycle_count"));
    bool newEnergyRateAvailable = QFileInfo::exists(m_devicePath + QStringLiteral("/power_now"));
    bool newTimeRemainingAvailable = QFileInfo::exists(m_devicePath + QStringLiteral("/energy_now"))
        && QFileInfo::exists(m_devicePath + QStringLiteral("/power_now"));
    const int batteryIndex = QFileInfo(m_devicePath).baseName().mid(3).toInt();

    if (!m_acpiPath.isEmpty()) {
        QProcess acpi;
        acpi.start(m_acpiPath, {QStringLiteral("-V")});
        if (acpi.waitForFinished(500) && acpi.exitStatus() == QProcess::NormalExit && acpi.exitCode() == 0) {
            const QString output = QString::fromLocal8Bit(acpi.readAllStandardOutput());
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const QRegularExpression batteryLine(
                QStringLiteral("^Battery\\s+%1:\\s+[^,]+,\\s+\\d+%(?:,\\s+(.+))?$").arg(batteryIndex));
            const QRegularExpression healthLine(
                QStringLiteral("^Battery\\s+%1:\\s+design capacity .* = (\\d+)%$").arg(batteryIndex));
            const QRegularExpression durationRegex(
                QStringLiteral("(\\d{1,2}:\\d{2}(?::\\d{2})?)"));

            for (const QString &rawLine : lines) {
                const QString line = rawLine.trimmed();
                const auto batteryMatch = batteryLine.match(line);
                if (batteryMatch.hasMatch()) {
                    const QString detail = batteryMatch.captured(1).trimmed();
                    if (!detail.isEmpty()) {
                        const auto durationMatch = durationRegex.match(detail);
                        if (durationMatch.hasMatch()) {
                            const QString durationText = durationMatch.captured(1);
                            QTime duration = QTime::fromString(durationText, QStringLiteral("HH:mm:ss"));
                            if (!duration.isValid()) {
                                duration = QTime::fromString(durationText, QStringLiteral("H:mm:ss"));
                            }
                            if (!duration.isValid()) {
                                duration = QTime::fromString(durationText, QStringLiteral("HH:mm"));
                            }
                            if (!duration.isValid()) {
                                duration = QTime::fromString(durationText, QStringLiteral("H:mm"));
                            }
                            if (duration.isValid()) {
                                newTimeRemaining = duration.hour() * 3600 + duration.minute() * 60 + duration.second();
                                newTimeRemainingAvailable = true;
                            }
                        }
                    }
                }

                const auto healthMatch = healthLine.match(line);
                if (healthMatch.hasMatch()) {
                    newHealth = healthMatch.captured(1).toInt();
                    newHealthAvailable = true;
                }
            }
        }
    }

    const bool newCharging = newStatus == QStringLiteral("Charging");
    const bool newDischarging = newStatus == QStringLiteral("Discharging")
        || newStatus == QStringLiteral("Not charging");
    const bool newFull = newStatus == QStringLiteral("Full");

    if (newStatus != m_status) {
        m_status = newStatus;
        emit statusChanged();
    }

    if (newCapacity != m_capacity) {
        m_capacity = newCapacity;
        emit capacityChanged();
    }

    if (newCharging != m_charging) {
        m_charging = newCharging;
        emit chargingChanged();
    }

    if (newDischarging != m_discharging) {
        m_discharging = newDischarging;
        emit dischargingChanged();
    }

    if (newFull != m_full) {
        m_full = newFull;
        emit fullChanged();
    }

    bool availabilityChanged = false;
    if (newHealthAvailable != m_healthAvailable) {
        m_healthAvailable = newHealthAvailable;
        availabilityChanged = true;
    }
    if (newCyclesAvailable != m_cyclesAvailable) {
        m_cyclesAvailable = newCyclesAvailable;
        availabilityChanged = true;
    }
    if (newEnergyRateAvailable != m_energyRateAvailable) {
        m_energyRateAvailable = newEnergyRateAvailable;
        availabilityChanged = true;
    }
    if (newTimeRemainingAvailable != m_timeRemainingAvailable) {
        m_timeRemainingAvailable = newTimeRemainingAvailable;
        availabilityChanged = true;
    }
    if (availabilityChanged) {
        emit supportChanged();
    }

    if (m_healthAvailable && newHealth != m_health) {
        m_health = newHealth;
        emit healthChanged();
    } else if (!m_healthAvailable && m_health != 0) {
        m_health = 0;
        emit healthChanged();
    }

    if (m_cyclesAvailable) {
        const int fileCycles = readFile(m_devicePath + QStringLiteral("/cycle_count")).toInt();
        if (fileCycles != m_cycles) {
            m_cycles = fileCycles;
            emit cyclesChanged();
        }
    } else if (m_cycles != 0) {
        m_cycles = 0;
        emit cyclesChanged();
    }

    const double energyNow = readFile(m_devicePath + QStringLiteral("/energy_now")).toDouble();
    const double powerNow = readFile(m_devicePath + QStringLiteral("/power_now")).toDouble();
    if (m_energyRateAvailable) {
        const double fileEnergyRate = powerNow / 1000000.0;
        if (qAbs(fileEnergyRate - m_energyRate) > 0.01) {
            m_energyRate = fileEnergyRate;
            emit energyRateChanged();
        }
    } else if (m_energyRate != 0.0) {
        m_energyRate = 0.0;
        emit energyRateChanged();
    }

    if (!newTimeRemainingAvailable && m_timeRemainingAvailable && powerNow > 0) {
        const double energyFull = readFile(m_devicePath + QStringLiteral("/energy_full")).toDouble();
        if (newCharging) {
            newTimeRemaining = static_cast<int>(((energyFull - energyNow) / powerNow) * 3600.0);
        } else if (newDischarging) {
            newTimeRemaining = static_cast<int>((energyNow / powerNow) * 3600.0);
        }
    }
    if (newTimeRemaining != m_timeRemaining) {
        m_timeRemaining = newTimeRemaining;
        emit timeRemainingChanged();
    }
}

QString BatteryModel::findBatteryDevice() const
{
    const QDir powerSupplyDir(QStringLiteral("/sys/class/power_supply"));
    const auto entries = powerSupplyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.startsWith(QStringLiteral("BAT"))) {
            return powerSupplyDir.absoluteFilePath(entry);
        }
    }
    return {};
}
