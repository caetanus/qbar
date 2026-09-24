#include "solar.h"

#include <QTimeZone>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double rad(double d) { return d * kPi / 180.0; }
double deg(double r) { return r * 180.0 / kPi; }

} // namespace

solar::DayEvents solar::dayEvents(const QDate &localDate, double latitudeDeg, double longitudeDeg)
{
    DayEvents ev;
    if (!localDate.isValid()) {
        return ev;
    }

    // Evaluate the solar position at local noon; the equation of time and declination
    // drift by well under a minute over the day, which is plenty for a colour schedule.
    const QDateTime localNoon(localDate, QTime(12, 0));
    const QDateTime utcNoon = localNoon.toUTC();
    const QDate utcDate = utcNoon.date();
    const double jd = utcDate.toJulianDay() - 0.5 + utcNoon.time().msecsSinceStartOfDay() / 86400000.0;
    const double jc = (jd - 2451545.0) / 36525.0;

    const double meanLong = std::fmod(280.46646 + jc * (36000.76983 + jc * 0.0003032), 360.0);
    const double meanAnom = 357.52911 + jc * (35999.05029 - 0.0001537 * jc);
    const double eccent = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);
    const double eqCentre = std::sin(rad(meanAnom)) * (1.914602 - jc * (0.004817 + 0.000014 * jc))
        + std::sin(rad(2 * meanAnom)) * (0.019993 - 0.000101 * jc)
        + std::sin(rad(3 * meanAnom)) * 0.000289;
    const double trueLong = meanLong + eqCentre;
    const double omega = 125.04 - 1934.136 * jc;
    const double appLong = trueLong - 0.00569 - 0.00478 * std::sin(rad(omega));
    const double meanObliq = 23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0;
    const double obliq = meanObliq + 0.00256 * std::cos(rad(omega));
    const double declin = deg(std::asin(std::sin(rad(obliq)) * std::sin(rad(appLong))));
    const double y = std::tan(rad(obliq / 2)) * std::tan(rad(obliq / 2));
    const double eqTime = 4.0 * deg(y * std::sin(2 * rad(meanLong))
        - 2 * eccent * std::sin(rad(meanAnom))
        + 4 * eccent * y * std::sin(rad(meanAnom)) * std::cos(2 * rad(meanLong))
        - 0.5 * y * y * std::sin(4 * rad(meanLong))
        - 1.25 * eccent * eccent * std::sin(2 * rad(meanAnom)));

    const double cosHa = std::cos(rad(90.833)) / (std::cos(rad(latitudeDeg)) * std::cos(rad(declin)))
        - std::tan(rad(latitudeDeg)) * std::tan(rad(declin));
    if (cosHa < -1.0) {
        ev.kind = DayEvents::Kind::PolarDay;
        return ev;
    }
    if (cosHa > 1.0) {
        ev.kind = DayEvents::Kind::PolarNight;
        return ev;
    }
    const double hourAngle = deg(std::acos(cosHa));
    // Minutes after 0h UTC of utcDate.
    const double solarNoon = 720.0 - 4.0 * longitudeDeg - eqTime;
    const QDateTime utcMidnight(utcDate, QTime(0, 0), QTimeZone::UTC);
    ev.sunrise = utcMidnight.addSecs(qRound64((solarNoon - hourAngle * 4.0) * 60.0)).toLocalTime();
    ev.sunset = utcMidnight.addSecs(qRound64((solarNoon + hourAngle * 4.0) * 60.0)).toLocalTime();
    return ev;
}
