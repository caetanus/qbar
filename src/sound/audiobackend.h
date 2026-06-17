#pragma once

#include <QObject>
#include <QString>

// Abstract audio backend exposing the contract consumed by Sound.qml. The base
// owns the default sink/source state and all the display/tooltip/icon
// formatting; concrete backends (libpulse SoundModel, WirePlumberBackend) only
// connect to the server, push state via applySinkState/applySourceState, and
// implement the volume/mute actions. Mirrors the WindowManagerBackend + factory
// pattern, and the chosen backend is selected at build time (see
// audiobackendfactory.h) — Sound.qml is agnostic to which one is live.
class AudioBackend : public QObject {
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
    explicit AudioBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioBackend() override = default;

    bool available() const { return m_available; }
    bool muted() const { return m_muted; }
    int volume() const { return m_volume; }
    QString sinkName() const { return m_sinkName; }
    QString displayText() const;
    QString tooltipText() const;
    QString outputTooltipText() const;
    bool sourceAvailable() const { return m_sourceAvailable; }
    bool sourceMuted() const { return m_sourceMuted; }
    int sourceVolume() const { return m_sourceVolume; }
    QString sourceName() const { return m_sourceName; }
    QString sourceDisplayText() const;
    QString sourceTooltipText() const;
    QString outputIconName() const;
    QString inputIconName() const;

    Q_INVOKABLE virtual void stepUp(int percent = 5) = 0;
    Q_INVOKABLE virtual void stepDown(int percent = 5) = 0;
    Q_INVOKABLE virtual void setPercent(int percent) = 0;
    Q_INVOKABLE virtual void toggleMute() = 0;
    Q_INVOKABLE virtual void stepSourceUp(int percent = 5) = 0;
    Q_INVOKABLE virtual void stepSourceDown(int percent = 5) = 0;
    Q_INVOKABLE virtual void setSourcePercent(int percent) = 0;
    Q_INVOKABLE virtual void toggleSourceMute() = 0;

signals:
    void volumeChanged();
    void availabilityChanged();

protected:
    struct State {
        QString name;
        QString description;
        QString formFactor;
        int volume = -1;
        bool muted = false;
        bool available = false;
        unsigned channels = 0;
    };

    // Diff against the current state, store it, and emit the right signals.
    // Concrete backends call these on the GUI thread.
    void applySinkState(const State &state);
    void applySourceState(const State &state);

    QString friendlySinkName() const;
    QString friendlySourceName() const;
    static QString formFactorIconName(const QString &formFactor);

    QString m_sinkName;
    QString m_sinkDescription;
    QString m_sinkFormFactor;
    int m_volume = -1;
    bool m_muted = false;
    bool m_available = false;
    unsigned m_sinkChannels = 0;
    QString m_sourceName;
    QString m_sourceDescription;
    QString m_sourceFormFactor;
    int m_sourceVolume = -1;
    bool m_sourceMuted = false;
    bool m_sourceAvailable = false;
    unsigned m_sourceChannels = 0;
};
