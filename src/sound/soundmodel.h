#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <pulse/pulseaudio.h>

#include "audiobackend.h"

// Talks to PulseAudio/PipeWire over its native socket via libpulse, on a
// dedicated pa_threaded_mainloop. It subscribes to server/sink/source events
// (no polling, no `pactl` subprocesses) and marshals state changes to the GUI
// thread, so the bar's event loop is never blocked. The default audio backend;
// see audiobackendfactory.h.
class SoundModel final : public AudioBackend {
    Q_OBJECT

public:
    explicit SoundModel(QObject *parent = nullptr);
    ~SoundModel() override;

    void stepUp(int percent = 5) override;
    void stepDown(int percent = 5) override;
    void setPercent(int percent) override;
    void toggleMute() override;
    void stepSourceUp(int percent = 5) override;
    void stepSourceDown(int percent = 5) override;
    void setSourcePercent(int percent) override;
    void toggleSourceMute() override;

private:
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

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    QTimer m_reconnectTimer;
};
