#include "soundmodel.h"

#include <QMetaObject>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kReconnectIntervalMs = 3000;

int volumePercent(const pa_cvolume &volume)
{
    const double avg = static_cast<double>(pa_cvolume_avg(&volume));
    return static_cast<int>(std::lround((avg * 100.0) / static_cast<double>(PA_VOLUME_NORM)));
}

QString formFactorOf(pa_proplist *proplist)
{
    if (proplist == nullptr) {
        return {};
    }
    const char *value = pa_proplist_gets(proplist, "device.form_factor");
    return value != nullptr ? QString::fromUtf8(value) : QString();
}

} // namespace

SoundModel::SoundModel(QObject *parent)
    : AudioBackend(parent)
{
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(kReconnectIntervalMs);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &SoundModel::connectToServer);

    m_mainloop = pa_threaded_mainloop_new();
    if (m_mainloop == nullptr || pa_threaded_mainloop_start(m_mainloop) < 0) {
        return; // libpulse unavailable; the model simply stays unavailable.
    }
    connectToServer();
}

SoundModel::~SoundModel()
{
    if (m_mainloop != nullptr) {
        pa_threaded_mainloop_stop(m_mainloop);
    }
    if (m_context != nullptr) {
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
    }
    if (m_mainloop != nullptr) {
        pa_threaded_mainloop_free(m_mainloop);
    }
}

void SoundModel::connectToServer()
{
    if (m_mainloop == nullptr) {
        return;
    }

    pa_threaded_mainloop_lock(m_mainloop);
    if (m_context != nullptr) {
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }

    m_context = pa_context_new(pa_threaded_mainloop_get_api(m_mainloop), "qbar");
    if (m_context == nullptr) {
        pa_threaded_mainloop_unlock(m_mainloop);
        scheduleReconnect();
        return;
    }

    pa_context_set_state_callback(m_context, &SoundModel::contextStateCallback, this);
    // NOFAIL: if the daemon isn't up yet, wait for it instead of failing.
    const int rc = pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_unlock(m_mainloop);

    if (rc < 0) {
        scheduleReconnect();
    }
}

void SoundModel::scheduleReconnect()
{
    // Always runs on the GUI thread (queued from the mainloop callback).
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

void SoundModel::markUnavailable()
{
    applySinkState(State{});
    applySourceState(State{});
}

void SoundModel::contextStateCallback(pa_context *context, void *userdata)
{
    auto *self = static_cast<SoundModel *>(userdata);
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(context, &SoundModel::subscribeCallback, self);
        pa_operation_unref(pa_context_subscribe(
            context,
            static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER),
            nullptr, nullptr));
        self->requestUpdate();
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        QMetaObject::invokeMethod(self, [self]() {
            self->markUnavailable();
            self->scheduleReconnect();
        }, Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

void SoundModel::subscribeCallback(pa_context *context, pa_subscription_event_type_t type, uint32_t, void *userdata)
{
    Q_UNUSED(context);
    const auto facility = static_cast<pa_subscription_event_type_t>(type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
    if (facility == PA_SUBSCRIPTION_EVENT_SINK
        || facility == PA_SUBSCRIPTION_EVENT_SOURCE
        || facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        static_cast<SoundModel *>(userdata)->requestUpdate();
    }
}

void SoundModel::requestUpdate()
{
    // Mainloop thread (called from callbacks). Query the current defaults.
    if (m_context == nullptr) {
        return;
    }
    pa_operation_unref(pa_context_get_server_info(m_context, &SoundModel::serverInfoCallback, this));
}

void SoundModel::serverInfoCallback(pa_context *context, const pa_server_info *info, void *userdata)
{
    auto *self = static_cast<SoundModel *>(userdata);
    if (info == nullptr) {
        return;
    }

    if (info->default_sink_name != nullptr && info->default_sink_name[0] != '\0') {
        pa_operation_unref(pa_context_get_sink_info_by_name(context, info->default_sink_name, &SoundModel::sinkInfoCallback, self));
    } else {
        QMetaObject::invokeMethod(self, [self]() { self->applySinkState(State{}); }, Qt::QueuedConnection);
    }

    if (info->default_source_name != nullptr && info->default_source_name[0] != '\0') {
        pa_operation_unref(pa_context_get_source_info_by_name(context, info->default_source_name, &SoundModel::sourceInfoCallback, self));
    } else {
        QMetaObject::invokeMethod(self, [self]() { self->applySourceState(State{}); }, Qt::QueuedConnection);
    }
}

void SoundModel::sinkInfoCallback(pa_context *, const pa_sink_info *info, int eol, void *userdata)
{
    if (eol != 0 || info == nullptr) {
        return;
    }
    auto *self = static_cast<SoundModel *>(userdata);
    State state;
    state.name = QString::fromUtf8(info->name);
    state.description = QString::fromUtf8(info->description);
    state.formFactor = formFactorOf(info->proplist);
    state.volume = std::clamp(volumePercent(info->volume), 0, 150);
    state.muted = info->mute != 0;
    state.channels = info->volume.channels;
    state.available = true;
    QMetaObject::invokeMethod(self, [self, state]() { self->applySinkState(state); }, Qt::QueuedConnection);
}

void SoundModel::sourceInfoCallback(pa_context *, const pa_source_info *info, int eol, void *userdata)
{
    if (eol != 0 || info == nullptr) {
        return;
    }
    auto *self = static_cast<SoundModel *>(userdata);
    // Skip monitor sources (they mirror sinks and aren't real inputs).
    if ((info->monitor_of_sink != PA_INVALID_INDEX)) {
        return;
    }
    State state;
    state.name = QString::fromUtf8(info->name);
    state.description = QString::fromUtf8(info->description);
    state.formFactor = formFactorOf(info->proplist);
    state.volume = std::clamp(volumePercent(info->volume), 0, 150);
    state.muted = info->mute != 0;
    state.channels = info->volume.channels;
    state.available = true;
    QMetaObject::invokeMethod(self, [self, state]() { self->applySourceState(state); }, Qt::QueuedConnection);
}

void SoundModel::setSinkVolume(int percent)
{
    if (!m_available || m_sinkName.isEmpty() || m_context == nullptr || m_mainloop == nullptr) {
        return;
    }
    const int target = std::clamp(percent, 0, 150);
    pa_cvolume volume;
    pa_cvolume_set(&volume, std::max(1u, m_sinkChannels),
                   static_cast<pa_volume_t>((static_cast<double>(target) / 100.0) * PA_VOLUME_NORM));

    pa_threaded_mainloop_lock(m_mainloop);
    pa_operation_unref(pa_context_set_sink_volume_by_name(m_context, m_sinkName.toUtf8().constData(), &volume, nullptr, nullptr));
    pa_threaded_mainloop_unlock(m_mainloop);
}

void SoundModel::setSourceVolume(int percent)
{
    if (!m_sourceAvailable || m_sourceName.isEmpty() || m_context == nullptr || m_mainloop == nullptr) {
        return;
    }
    const int target = std::clamp(percent, 0, 150);
    pa_cvolume volume;
    pa_cvolume_set(&volume, std::max(1u, m_sourceChannels),
                   static_cast<pa_volume_t>((static_cast<double>(target) / 100.0) * PA_VOLUME_NORM));

    pa_threaded_mainloop_lock(m_mainloop);
    pa_operation_unref(pa_context_set_source_volume_by_name(m_context, m_sourceName.toUtf8().constData(), &volume, nullptr, nullptr));
    pa_threaded_mainloop_unlock(m_mainloop);
}

void SoundModel::stepUp(int percent)
{
    setSinkVolume(m_volume + std::max(1, percent));
}

void SoundModel::stepDown(int percent)
{
    setSinkVolume(m_volume - std::max(1, percent));
}

void SoundModel::setPercent(int percent)
{
    setSinkVolume(percent);
}

void SoundModel::toggleMute()
{
    if (!m_available || m_sinkName.isEmpty() || m_context == nullptr || m_mainloop == nullptr) {
        return;
    }
    pa_threaded_mainloop_lock(m_mainloop);
    pa_operation_unref(pa_context_set_sink_mute_by_name(m_context, m_sinkName.toUtf8().constData(), m_muted ? 0 : 1, nullptr, nullptr));
    pa_threaded_mainloop_unlock(m_mainloop);
}

void SoundModel::stepSourceUp(int percent)
{
    setSourceVolume(m_sourceVolume + std::max(1, percent));
}

void SoundModel::stepSourceDown(int percent)
{
    setSourceVolume(m_sourceVolume - std::max(1, percent));
}

void SoundModel::setSourcePercent(int percent)
{
    setSourceVolume(percent);
}

void SoundModel::toggleSourceMute()
{
    if (!m_sourceAvailable || m_sourceName.isEmpty() || m_context == nullptr || m_mainloop == nullptr) {
        return;
    }
    pa_threaded_mainloop_lock(m_mainloop);
    pa_operation_unref(pa_context_set_source_mute_by_name(m_context, m_sourceName.toUtf8().constData(), m_sourceMuted ? 0 : 1, nullptr, nullptr));
    pa_threaded_mainloop_unlock(m_mainloop);
}
