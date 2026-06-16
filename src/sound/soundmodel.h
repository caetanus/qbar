#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <pulse/pulseaudio.h>

// Talks to PulseAudio/PipeWire over its native socket via libpulse, on a
// dedicated pa_threaded_mainloop. It subscribes to server/sink/source events
// (no polling, no `pactl` subprocesses) and marshals state changes to the GUI
// thread, so the bar's event loop is never blocked.
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
    ~SoundModel() override;

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
        QString name;
        QString description;
        QString formFactor;
        int volume = -1;
        bool muted = false;
        bool available = false;
        unsigned channels = 0;
    };

    QString friendlySinkName() const;
    QString friendlySourceName() const;
    static QString formFactorIconName(const QString &formFactor);

    // GUI thread (queued from the mainloop thread).
    void applySinkState(const State &state);
    void applySourceState(const State &state);

    // libpulse callbacks (run on the mainloop thread).
    static void contextStateCallback(pa_context *context, void *userdata);
    static void subscribeCallback(pa_context *context, pa_subscription_event_type_t type, uint32_t index, void *userdata);
    static void serverInfoCallback(pa_context *context, const pa_server_info *info, void *userdata);
    static void sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void sourceInfoCallback(pa_context *context, const pa_source_info *info, int eol, void *userdata);
    void requestUpdate(); // mainloop thread: query server info → sink/source

    // Connection lifecycle (GUI thread). If the daemon/socket dies the context
    // fails; we mark everything unavailable and keep retrying every few seconds.
    void connectToServer();
    void scheduleReconnect();
    void markUnavailable();

    // Volume control (GUI thread): lock mainloop, issue async op, unlock.
    void setSinkVolume(int percent);
    void setSourceVolume(int percent);

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

    QString m_defaultSinkName;
    QString m_defaultSourceName;

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    QTimer m_reconnectTimer;
};
