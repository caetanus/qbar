#pragma once

// Correlated colour temperature → RGB whitepoint multipliers (each in 0..1, the
// brightest channel pinned at 1 so the screen keeps its brightness and only shifts
// hue). 6500 K is the identity (D65, the sRGB white).
namespace colortemp {

struct Whitepoint {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

constexpr int kMinKelvin = 1000;
constexpr int kMaxKelvin = 25000;
constexpr int kNeutralKelvin = 6500;

Whitepoint whitepoint(int kelvin);

} // namespace colortemp
