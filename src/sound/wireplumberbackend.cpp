// WirePlumber/GLib headers must precede any Qt header: Qt's `signals`/`slots`
// keyword macros otherwise clash with identifiers used inside glib/pipewire.
#include <wp/wp.h>

#include "wireplumberbackend.h"

#include <algorithm>
#include <cmath>

namespace {

// mixer-api volume scale: 0 = linear, 1 = cubic. Cubic matches pavucontrol/wpctl
// so the percentages line up with what users expect.
constexpr int kMixerScaleCubic = 1;
constexpr int kMaxPercent = 150;

QString propString(WpPipewireObject *object, const char *key)
{
    if (object == nullptr) {
        return {};
    }
    const gchar *value = wp_pipewire_object_get_property(object, key);
    return value != nullptr ? QString::fromUtf8(value) : QString();
}

} // namespace

WirePlumberBackend::WirePlumberBackend(QObject *parent)
    : AudioBackend(parent)
{
    wp_init(WP_INIT_PIPEWIRE);
    m_core = wp_core_new(nullptr, nullptr, nullptr);
    if (m_core == nullptr || wp_core_connect(m_core) == FALSE) {
        return; // WirePlumber/PipeWire unavailable; stays unavailable.
    }

    // Track nodes and devices so we can resolve the default sink/source name,
    // description and form factor (icon) once we know the default node id.
    m_om = wp_object_manager_new();
    wp_object_manager_add_interest(m_om, WP_TYPE_NODE, nullptr);
    wp_object_manager_add_interest(m_om, WP_TYPE_DEVICE, nullptr);
    wp_object_manager_request_object_features(m_om, WP_TYPE_NODE,
                                              WP_PIPEWIRE_OBJECT_FEATURES_ALL);
    wp_object_manager_request_object_features(m_om, WP_TYPE_DEVICE,
                                              WP_PIPEWIRE_OBJECT_FEATURES_ALL);

    // Load the two helper modules; each calls back when ready.
    m_pendingPlugins = 2;
    wp_core_load_component(m_core, "libwireplumber-module-default-nodes-api", "module",
                           nullptr, nullptr, nullptr,
                           reinterpret_cast<GAsyncReadyCallback>(&WirePlumberBackend::onPluginLoaded), this);
    wp_core_load_component(m_core, "libwireplumber-module-mixer-api", "module",
                           nullptr, nullptr, nullptr,
                           reinterpret_cast<GAsyncReadyCallback>(&WirePlumberBackend::onPluginLoaded), this);
}

WirePlumberBackend::~WirePlumberBackend()
{
    g_clear_object(&m_om);
    g_clear_object(&m_mixer);
    g_clear_object(&m_defaultNodes);
    if (m_core != nullptr) {
        wp_core_disconnect(m_core);
        g_clear_object(&m_core);
    }
}

void WirePlumberBackend::onPluginLoaded(void *, void *result, void *data)
{
    auto *self = static_cast<WirePlumberBackend *>(data);
    g_autoptr(GError) error = nullptr;
    if (wp_core_load_component_finish(self->m_core, static_cast<GAsyncResult *>(result), &error) == FALSE) {
        g_warning("qbar: WirePlumber component failed to load: %s", error != nullptr ? error->message : "");
    }
    if (--self->m_pendingPlugins == 0) {
        self->maybeActivate();
    }
}

void WirePlumberBackend::maybeActivate()
{
    m_mixer = wp_plugin_find(m_core, "mixer-api");
    m_defaultNodes = wp_plugin_find(m_core, "default-nodes-api");
    if (m_mixer == nullptr || m_defaultNodes == nullptr) {
        g_warning("qbar: WirePlumber mixer-api/default-nodes-api unavailable");
        return;
    }
    g_object_set(G_OBJECT(m_mixer), "scale", kMixerScaleCubic, nullptr);

    g_signal_connect(m_defaultNodes, "changed",
                     G_CALLBACK(&WirePlumberBackend::onDefaultNodesChanged), this);
    g_signal_connect(m_mixer, "changed",
                     G_CALLBACK(&WirePlumberBackend::onMixerChanged), this);

    g_signal_connect(m_om, "installed",
                     G_CALLBACK(&WirePlumberBackend::onObjectManagerInstalled), this);
    wp_core_install_object_manager(m_core, m_om);
}

void WirePlumberBackend::onObjectManagerInstalled(void *, void *data)
{
    auto *self = static_cast<WirePlumberBackend *>(data);
    self->m_ready = true;
    self->refresh();
}

void WirePlumberBackend::onDefaultNodesChanged(void *, void *data)
{
    static_cast<WirePlumberBackend *>(data)->refresh();
}

void WirePlumberBackend::onMixerChanged(void *, unsigned, void *data)
{
    static_cast<WirePlumberBackend *>(data)->refresh();
}

guint WirePlumberBackend::defaultNodeId(const char *mediaClass)
{
    if (m_defaultNodes == nullptr) {
        return 0;
    }
    guint id = 0;
    g_signal_emit_by_name(m_defaultNodes, "get-default-node", mediaClass, &id);
    return (id == G_MAXUINT) ? 0 : id;
}

AudioBackend::State WirePlumberBackend::readNode(const char *mediaClass)
{
    State state;
    const guint id = defaultNodeId(mediaClass);
    if (id == 0 || m_mixer == nullptr) {
        return state;
    }

    GVariant *volumeVariant = nullptr;
    g_signal_emit_by_name(m_mixer, "get-volume", id, &volumeVariant);
    if (volumeVariant == nullptr) {
        return state;
    }
    double volume = 0.0;
    gboolean mute = FALSE;
    g_variant_lookup(volumeVariant, "volume", "d", &volume);
    g_variant_lookup(volumeVariant, "mute", "b", &mute);
    g_clear_pointer(&volumeVariant, g_variant_unref);

    state.available = true;
    state.volume = std::clamp(static_cast<int>(std::lround(volume * 100.0)), 0, kMaxPercent);
    state.muted = mute != FALSE;

    // Resolve name/description/form-factor from the node (and its device).
    auto *node = static_cast<WpPipewireObject *>(
        wp_object_manager_lookup(m_om, WP_TYPE_NODE,
                                 WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, nullptr));
    if (node != nullptr) {
        state.name = propString(node, "node.name");
        state.description = propString(node, "node.description");
        if (state.description.isEmpty()) {
            state.description = propString(node, "node.nick");
        }
        state.formFactor = propString(node, "device.form-factor");
        if (state.formFactor.isEmpty()) {
            const QString deviceId = propString(node, "device.id");
            if (!deviceId.isEmpty()) {
                auto *device = static_cast<WpPipewireObject *>(
                    wp_object_manager_lookup(m_om, WP_TYPE_DEVICE,
                                             WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
                                             deviceId.toUInt(), nullptr));
                if (device != nullptr) {
                    state.formFactor = propString(device, "device.form-factor");
                    g_object_unref(device);
                }
            }
        }
        g_object_unref(node);
    }
    return state;
}

void WirePlumberBackend::refresh()
{
    if (!m_ready) {
        return;
    }
    applySinkState(readNode("Audio/Sink"));
    applySourceState(readNode("Audio/Source"));
}

void WirePlumberBackend::setNodeVolume(const char *mediaClass, int percent)
{
    const guint id = defaultNodeId(mediaClass);
    if (id == 0 || m_mixer == nullptr) {
        return;
    }
    const double target = std::clamp(percent, 0, kMaxPercent) / 100.0;

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "volume", g_variant_new_double(target));
    GVariant *variant = g_variant_ref_sink(g_variant_builder_end(&builder));
    gboolean ok = FALSE;
    g_signal_emit_by_name(m_mixer, "set-volume", id, variant, &ok);
    g_variant_unref(variant);
}

void WirePlumberBackend::toggleNodeMute(const char *mediaClass, bool currentlyMuted)
{
    const guint id = defaultNodeId(mediaClass);
    if (id == 0 || m_mixer == nullptr) {
        return;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "mute", g_variant_new_boolean(currentlyMuted ? FALSE : TRUE));
    GVariant *variant = g_variant_ref_sink(g_variant_builder_end(&builder));
    gboolean ok = FALSE;
    g_signal_emit_by_name(m_mixer, "set-volume", id, variant, &ok);
    g_variant_unref(variant);
}

void WirePlumberBackend::stepUp(int percent)
{
    setNodeVolume("Audio/Sink", m_volume + std::max(1, percent));
}

void WirePlumberBackend::stepDown(int percent)
{
    setNodeVolume("Audio/Sink", m_volume - std::max(1, percent));
}

void WirePlumberBackend::setPercent(int percent)
{
    setNodeVolume("Audio/Sink", percent);
}

void WirePlumberBackend::toggleMute()
{
    toggleNodeMute("Audio/Sink", m_muted);
}

void WirePlumberBackend::stepSourceUp(int percent)
{
    setNodeVolume("Audio/Source", m_sourceVolume + std::max(1, percent));
}

void WirePlumberBackend::stepSourceDown(int percent)
{
    setNodeVolume("Audio/Source", m_sourceVolume - std::max(1, percent));
}

void WirePlumberBackend::setSourcePercent(int percent)
{
    setNodeVolume("Audio/Source", percent);
}

void WirePlumberBackend::toggleSourceMute()
{
    toggleNodeMute("Audio/Source", m_sourceMuted);
}
