#include "platformbarintegration.h"

#include <QGuiApplication>
#include <QWidget>
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
    QWidget *window = nullptr;
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

void layerSurfaceConfigure(void *, zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t, uint32_t)
{
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

void layerSurfaceClosed(void *, zwlr_layer_surface_v1 *)
{
    if (state.window != nullptr) {
        state.window->close();
    }
}

const zwlr_layer_surface_v1_listener layerSurfaceListener = {
    layerSurfaceConfigure,
    layerSurfaceClosed,
};

wl_surface *surfaceFor(QWidget *window)
{
    auto *native = QGuiApplication::platformNativeInterface();
    if (native == nullptr || window->windowHandle() == nullptr) {
        return nullptr;
    }

    return static_cast<wl_surface *>(native->nativeResourceForWindow(QByteArrayLiteral("surface"), window->windowHandle()));
}

} // namespace

bool applyWaylandLayerShellIntegration(QWidget *window, const BarConfig &config)
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

    zwlr_layer_surface_v1_add_listener(state.layerSurface, &layerSurfaceListener, nullptr);
    const bool bottom = config.position == BarPosition::Bottom;
    const uint32_t verticalAnchor = bottom
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    zwlr_layer_surface_v1_set_anchor(
        state.layerSurface,
        verticalAnchor
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(state.layerSurface, 0, static_cast<uint32_t>(config.height));
    zwlr_layer_surface_v1_set_exclusive_zone(state.layerSurface, config.exclusiveZone ? config.height : 0);
    zwlr_layer_surface_v1_set_margin(state.layerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit(surface);
    wl_display_flush(state.display);
    return true;
}
