#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>

class SoundModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QString sinkName READ sinkName NOTIFY availabilityChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY volumeChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY volumeChanged)
    Q_PROPERTY(QString outputTooltipText READ outputTooltipText NOTIFY volumeChanged)
    Q_PROPERTY(bool sourceAvailable READ sourceAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool sourceMuted READ sourceMuted NOTIFY volumeChanged)
    Q_PROPERTY(int sourceVolume READ sourceVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY availabilityChanged)
    Q_PROPERTY(QString sourceDisplayText READ sourceDisplayText NOTIFY volumeChanged)
    Q_PROPERTY(QString sourceTooltipText READ sourceTooltipText NOTIFY volumeChanged)
    Q_PROPERTY(QString outputIconName READ outputIconName NOTIFY volumeChanged)
    Q_PROPERTY(QString inputIconName READ inputIconName NOTIFY volumeChanged)

public:
    explicit SoundModel(QObject *parent = nullptr);

    bool available() const;
    bool muted() const;
    int volume() const;
    QString sinkName() const;
    QString displayText() const;
    QString tooltipText() const;
    QString outputTooltipText() const;
    bool sourceAvailable() const;
    bool sourceMuted() const;
    int sourceVolume() const;
    QString sourceName() const;
    QString sourceDisplayText() const;
    QString sourceTooltipText() const;
    QString outputIconName() const;
    QString inputIconName() const;

    Q_INVOKABLE void stepUp(int percent = 5);
    Q_INVOKABLE void stepDown(int percent = 5);
    Q_INVOKABLE void setPercent(int percent);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void stepSourceUp(int percent = 5);
    Q_INVOKABLE void stepSourceDown(int percent = 5);
    Q_INVOKABLE void setSourcePercent(int percent);
    Q_INVOKABLE void toggleSourceMute();

signals:
    void volumeChanged();
    void availabilityChanged();

private:
    struct State {
        QString sinkName;
        QString formFactor;
        int volume = -1;
        bool muted = false;
        bool available = false;
    };

    struct DeviceInfo {
        QString description;
        QString formFactor;
    };

    static QString runPactl(const QStringList &args, bool *ok = nullptr);
    static QString defaultSinkName();
    static QString defaultSourceName();
    static QString friendlySinkName(const QString &sinkName);
    static QString friendlySourceName(const QString &sourceName);
    static DeviceInfo deviceInfo(const QString &listOutput, const QString &deviceName);
    static QString formFactorIconName(const QString &formFactor);
    static int parseVolumePercent(const QString &output);
    static bool parseMuted(const QString &output);

    void refresh();
    void setState(const State &state);
    void setSourceState(const State &state);
    void adjustPercent(int deltaPercent);
    void adjustSourcePercent(int deltaPercent);

    QString m_sinkName;
    QString m_sinkFormFactor;
    int m_volume = -1;
    bool m_muted = false;
    bool m_available = false;
    QString m_sourceName;
    QString m_sourceFormFactor;
    int m_sourceVolume = -1;
    bool m_sourceMuted = false;
    bool m_sourceAvailable = false;
    QTimer m_timer;
};
