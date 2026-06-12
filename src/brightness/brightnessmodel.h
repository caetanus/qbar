#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

class BrightnessModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    Q_PROPERTY(int maxBrightness READ maxBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int percent READ percent NOTIFY brightnessChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY availabilityChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY brightnessChanged)

public:
    explicit BrightnessModel(QObject *parent = nullptr);

    bool available() const;
    int brightness() const;
    int maxBrightness() const;
    int percent() const;
    QString deviceName() const;
    QString tooltipText() const;

    Q_INVOKABLE void stepUp(int percent = 5);
    Q_INVOKABLE void stepDown(int percent = 5);
    Q_INVOKABLE void setPercent(int percent);

signals:
    void brightnessChanged();
    void availabilityChanged();

private:
    struct DeviceState {
        QString path;
        QString name;
        int brightness = -1;
        int maxBrightness = -1;
        bool writable = false;
        bool valid = false;
    };

    static int readIntegerFile(const QString &path);
    static bool writeIntegerFile(const QString &path, int value);
    static QString selectBrightnessDevice();
    DeviceState readDeviceState(const QString &path) const;
    void refresh();
    void setState(const DeviceState &state);
    void adjustPercent(int deltaPercent);

    QString m_devicePath;
    QString m_deviceName;
    int m_brightness = -1;
    int m_maxBrightness = -1;
    bool m_available = false;
    bool m_writable = false;
    QTimer m_timer;
};

