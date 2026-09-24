#include "colortemp.h"

#include <algorithm>
#include <cmath>

namespace {

struct Xy {
    double x;
    double y;
};

// CIE daylight locus (illuminant D series), valid 4000–25000 K. Hits D65 at 6500 K,
// which is what makes "day" an exact identity on an sRGB display.
Xy daylightLocus(double t)
{
    const double x = t <= 7000.0
        ? 0.244063 + 0.09911e3 / t + 2.9678e6 / (t * t) - 4.6070e9 / (t * t * t)
        : 0.237040 + 0.24748e3 / t + 1.9018e6 / (t * t) - 2.0064e9 / (t * t * t);
    const double y = -3.000 * x * x + 2.870 * x - 0.275;
    return {x, y};
}

// Planckian (black-body) locus, Kim et al. 2002 approximation, valid 1667–4000 K here.
Xy planckianLocus(double t)
{
    const double x = -0.2661239e9 / (t * t * t) - 0.2343589e6 / (t * t) + 0.8776956e3 / t + 0.179910;
    const double y = t <= 2222.0
        ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
        : -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
    return {x, y};
}

colortemp::Whitepoint linearSrgb(Xy c)
{
    // xyY (Y = 1) → XYZ → linear sRGB (D65 matrix).
    const double X = c.x / c.y;
    const double Y = 1.0;
    const double Z = (1.0 - c.x - c.y) / c.y;
    colortemp::Whitepoint wp;
    wp.r = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    wp.g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    wp.b = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
    return wp;
}

// Linear light → sRGB-encoded. The gamma ramp multiplies the display's already
// gamma-encoded values, so the multiplier must live in that same encoded space; this is
// what lines the Kelvin scale up with redshift's (whose table is encoded sRGB).
double srgbEncode(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

colortemp::Whitepoint rawWhitepoint(double t)
{
    if (t >= 4000.0) {
        return linearSrgb(daylightLocus(t));
    }
    // The two loci disagree slightly where they meet, so rescale the black-body side
    // per channel to be continuous with the daylight side at 4000 K.
    const colortemp::Whitepoint day4000 = linearSrgb(daylightLocus(4000.0));
    const colortemp::Whitepoint bb4000 = linearSrgb(planckianLocus(4000.0));
    colortemp::Whitepoint wp = linearSrgb(planckianLocus(std::max(t, 1667.0)));
    wp.r *= day4000.r / bb4000.r;
    wp.g *= day4000.g / bb4000.g;
    wp.b *= day4000.b / bb4000.b;
    return wp;
}

} // namespace

colortemp::Whitepoint colortemp::whitepoint(int kelvin)
{
    if (kelvin == kNeutralKelvin) {
        return {};
    }
    const double t = std::clamp(kelvin, kMinKelvin, kMaxKelvin);
    Whitepoint wp = rawWhitepoint(t);
    const double peak = std::max({wp.r, wp.g, wp.b, 1e-6});
    wp.r = srgbEncode(wp.r / peak);
    wp.g = srgbEncode(wp.g / peak);
    wp.b = srgbEncode(wp.b / peak);
    return wp;
}
