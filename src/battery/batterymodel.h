#pragma once

#include <QObject>
#include <QTimer>

class BatteryModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int capacity READ capacity NOTIFY capacityChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)
    Q_PROPERTY(bool discharging READ discharging NOTIFY dischargingChanged)
    Q_PROPERTY(bool full READ full NOTIFY fullChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool healthAvailable READ healthAvailable NOTIFY supportChanged)
    Q_PROPERTY(bool cyclesAvailable READ cyclesAvailable NOTIFY supportChanged)
    Q_PROPERTY(bool energyRateAvailable READ energyRateAvailable NOTIFY supportChanged)
    Q_PROPERTY(bool timeRemainingAvailable READ timeRemainingAvailable NOTIFY supportChanged)
    Q_PROPERTY(int health READ health NOTIFY healthChanged)
    Q_PROPERTY(int cycles READ cycles NOTIFY cyclesChanged)
    Q_PROPERTY(double energyRate READ energyRate NOTIFY energyRateChanged)
    Q_PROPERTY(int timeRemaining READ timeRemaining NOTIFY timeRemainingChanged)

public:
    explicit BatteryModel(QObject *parent = nullptr);

    int capacity() const;
    QString status() const;
    bool charging() const;
    bool discharging() const;
    bool full() const;
    bool available() const;
    bool healthAvailable() const;
    bool cyclesAvailable() const;
    bool energyRateAvailable() const;
    bool timeRemainingAvailable() const;
    int health() const;
    int cycles() const;
    double energyRate() const;
    int timeRemaining() const;

signals:
    void capacityChanged();
    void statusChanged();
    void chargingChanged();
    void dischargingChanged();
    void fullChanged();
    void supportChanged();
    void healthChanged();
    void cyclesChanged();
    void energyRateChanged();
    void timeRemainingChanged();

private:
    void refresh();
    QString findBatteryDevice() const;

    int m_capacity = 0;
    QString m_status;
    bool m_charging = false;
    bool m_discharging = true;
    bool m_full = false;
    int m_health = 100;
    int m_cycles = 0;
    double m_energyRate = 0.0;
    int m_timeRemaining = 0;
    bool m_healthAvailable = false;
    bool m_cyclesAvailable = false;
    bool m_energyRateAvailable = false;
    bool m_timeRemainingAvailable = false;
    QString m_acpiPath;
    QString m_devicePath;
    QTimer m_timer;
};
