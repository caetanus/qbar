#pragma once

#include <QObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "colortemp.h"

// A display-gamma sink for RedshiftModel: hands every output a ramp scaled by the
// current whitepoint. One implementation per platform (wlr-gamma-control on
// wlroots/Hyprland, XRandR on X11); the model never sees the difference.
class GammaBackend : public QObject {
    Q_OBJECT

public:
    explicit GammaBackend(QObject *parent = nullptr) : QObject(parent) {}

    virtual QString name() const = 0;
    // At least one output is (or can be) under our control.
    virtual bool available() const = 0;
    // Short human hint for the tooltip ("2 outputs", "owned by another client", …).
    virtual QString status() const = 0;
    // Take (or keep) control of every output and push this whitepoint. Outputs that
    // appear later inherit it.
    virtual void apply(const colortemp::Whitepoint &wp) = 0;
    // Give the outputs back: the compositor restores its own tables (Wayland) or we
    // write identity ramps (X11).
    virtual void release() = 0;

signals:
    void availabilityChanged();
};

// Fill three consecutive ramps (r, g, b) of `size` entries: a linear ramp scaled by the
// whitepoint — the same table redshift and wlsunset produce.
inline void fillGammaRamps(uint16_t *table, std::size_t size, const colortemp::Whitepoint &wp)
{
    uint16_t *r = table;
    uint16_t *g = table + size;
    uint16_t *b = table + 2 * size;
    for (std::size_t i = 0; i < size; ++i) {
        const double v = size > 1 ? static_cast<double>(i) / static_cast<double>(size - 1) : 1.0;
        r[i] = static_cast<uint16_t>(std::lround(std::clamp(v * wp.r, 0.0, 1.0) * 65535.0));
        g[i] = static_cast<uint16_t>(std::lround(std::clamp(v * wp.g, 0.0, 1.0) * 65535.0));
        b[i] = static_cast<uint16_t>(std::lround(std::clamp(v * wp.b, 0.0, 1.0) * 65535.0));
    }
}

// Picks the backend for the running QPA platform. nullptr when nothing applies (no
// gamma protocol in the compositor, or the platform was not compiled in).
GammaBackend *createGammaBackend(QObject *parent);
