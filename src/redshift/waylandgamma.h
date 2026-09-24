#pragma once

#ifdef QBAR_HAVE_WAYLAND

#include "gammabackend.h"

#include <cstdint>
#include <memory>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_output;
struct zwlr_gamma_control_manager_v1;
struct zwlr_gamma_control_v1;

// wlr-gamma-control-unstable-v1 (sway, Hyprland, river, …). Binds its own registry on
// Qt's wl_display so output hotplug keeps working; the events arrive through Qt's
// normal dispatch of the default queue, on the GUI thread.
class WaylandGammaBackend final : public GammaBackend {
public:
    explicit WaylandGammaBackend(QObject *parent = nullptr);
    ~WaylandGammaBackend() override;

    // Binds the registry and does one roundtrip; false without a display or when the
    // compositor doesn't advertise the gamma-control manager.
    bool init();

    QString name() const override;
    bool available() const override;
    QString status() const override;
    void apply(const colortemp::Whitepoint &wp) override;
    void release() override;

    struct Output {
        WaylandGammaBackend *self = nullptr;
        uint32_t globalName = 0;
        wl_output *output = nullptr;
        zwlr_gamma_control_v1 *control = nullptr;
        uint32_t rampSize = 0;
        bool failed = false;  // the compositor refused (another client owns gamma)
    };

    // Entry points for the C listeners.
    void handleGlobal(uint32_t name, const char *interface, uint32_t version);
    void handleGlobalRemove(uint32_t name);
    void handleGammaSize(Output *output, uint32_t size);
    void handleFailed(Output *output);

private:
    void ensureControl(Output *output);
    void destroyControl(Output *output);
    void sendRamps(Output *output);
    void notifyAvailability();

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    zwlr_gamma_control_manager_v1 *m_manager = nullptr;
    std::vector<std::unique_ptr<Output>> m_outputs;
    colortemp::Whitepoint m_whitepoint;
    bool m_active = false;
};

#endif // QBAR_HAVE_WAYLAND
