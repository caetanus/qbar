#include "platformbarintegration.h"

#include <QGuiApplication>
#include <QSize>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <algorithm>
#include <cstring>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {

struct LayerShellState {
    wl_display *display = nullptr;
    zwlr_layer_shell_v1 *layerShell = nullptr;
    zwlr_layer_surface_v1 *layerSurface = nullptr;
    QWindow *window = nullptr;
};

LayerShellState state;

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *shellState = static_cast<LayerShellState *>(data);
    if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        shellState->layerShell = static_cast<zwlr_layer_shell_v1 *>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4U)));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

// The compositor tells us the actual negotiated surface size here.
// Resize the Qt window to match so QML lays out at the right dimensions.
void layerSurfaceConfigure(void *data, zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    auto *s = static_cast<LayerShellState *>(data);
    if (s && s->window && width > 0 && height > 0) {
        const QSize sz(static_cast<int>(width), static_cast<int>(height));
        s->window->setMinimumSize(sz);
        s->window->setMaximumSize(sz);
        s->window->resize(sz);
    }
}

void layerSurfaceClosed(void *data, zwlr_layer_surface_v1 *)
{
    auto *s = static_cast<LayerShellState *>(data);
    if (s && s->window)
        s->window->close();
}

const zwlr_layer_surface_v1_listener layerSurfaceListener = {
    layerSurfaceConfigure,
    layerSurfaceClosed,
};

wl_surface *surfaceFor(QWindow *window)
{
    auto *native = QGuiApplication::platformNativeInterface();
    if (native == nullptr || window == nullptr) {
        return nullptr;
    }

    return static_cast<wl_surface *>(native->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
}

} // namespace

bool applyWaylandLayerShellIntegration(QWindow *window, const BarConfig &config)
{
    if (!config.waylandLayerShell) {
        return false;
    }

    auto *native = QGuiApplication::platformNativeInterface();
    auto *display = native != nullptr
        ? static_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("display")))
        : nullptr;
    if (display == nullptr) {
        return false;
    }

    wl_surface *surface = surfaceFor(window);
    if (surface == nullptr) {
        return false;
    }

    if (state.layerSurface != nullptr) {
        return true;
    }

    state.display = display;
    state.window = window;

    wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registryListener, &state);
    wl_display_roundtrip(state.display);
    wl_registry_destroy(registry);

    if (state.layerShell == nullptr) {
        return false;
    }

    state.layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        state.layerShell,
        surface,
        nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        "qbar");

    // Pass &state so both callbacks can reach the window pointer
    zwlr_layer_surface_v1_add_listener(state.layerSurface, &layerSurfaceListener, &state);

    const bool bottom = config.position == BarPosition::Bottom;
    const uint32_t verticalAnchor = bottom
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    zwlr_layer_surface_v1_set_anchor(
        state.layerSurface,
        verticalAnchor
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

    // Width = 0 lets the compositor decide (honoring margins below)
    zwlr_layer_surface_v1_set_size(state.layerSurface, 0, static_cast<uint32_t>(config.height));

    // Per-side margins: use explicit margin-top/bottom/left/right if set, else fall back to margin
    auto effectiveMargin = [&](int perSide) -> uint32_t {
        return static_cast<uint32_t>(std::max(0, perSide >= 0 ? perSide : config.margin));
    };
    const uint32_t mTop    = effectiveMargin(bottom ? config.marginBottom : config.marginTop);
    const uint32_t mBottom = effectiveMargin(bottom ? config.marginTop    : config.marginBottom);
    const uint32_t mLeft   = effectiveMargin(config.marginLeft);
    const uint32_t mRight  = effectiveMargin(config.marginRight);

    zwlr_layer_surface_v1_set_margin(state.layerSurface,
        bottom ? 0    : mTop,
        mRight,
        bottom ? mTop : 0,
        mLeft);

    // Reserve space: bar height + the edge-facing margin so windows don't overlap the gap
    const uint32_t edgeMargin = bottom ? mBottom : mTop;
    zwlr_layer_surface_v1_set_exclusive_zone(
        state.layerSurface,
        config.exclusiveZone ? config.height + static_cast<int>(edgeMargin) : 0);

    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit(surface);
    wl_display_flush(state.display);
    return true;
}
