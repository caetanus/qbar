#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class TemperatureModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cpuTemperature READ cpuTemperature NOTIFY temperaturesChanged)
    Q_PROPERTY(int gpuTemperature READ gpuTemperature NOTIFY temperaturesChanged)
    Q_PROPERTY(bool cpuAvailable READ cpuAvailable NOTIFY temperaturesChanged)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY temperaturesChanged)
    Q_PROPERTY(bool available READ available NOTIFY temperaturesChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY temperaturesChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY temperaturesChanged)

public:
    explicit TemperatureModel(QObject *parent = nullptr);

    int cpuTemperature() const;
    int gpuTemperature() const;
    bool cpuAvailable() const;
    bool gpuAvailable() const;
    bool available() const;
    QString displayText() const;
    QString tooltipText() const;

signals:
    void temperaturesChanged();

private:
    struct SensorReading {
        QString deviceName;
        QString sensorLabel;
        int celsius = 0;
        bool valid = false;
        int cpuScore = 0;
        int gpuScore = 0;
    };

    QVector<SensorReading> readSensorReadings() const;
    SensorReading bestReading(const QVector<SensorReading> &readings, bool cpu) const;
    void refresh();

    static QString readTextFile(const QString &path);
    static int scoreCpuSensor(const QString &deviceName, const QString &sensorLabel);
    static int scoreGpuSensor(const QString &deviceName, const QString &sensorLabel);
    static QString readingName(const SensorReading &reading);

    int m_cpuTemperature = 0;
    int m_gpuTemperature = 0;
    QString m_cpuName;
    QString m_gpuName;
    bool m_cpuAvailable = false;
    bool m_gpuAvailable = false;
    QTimer m_timer;
};
