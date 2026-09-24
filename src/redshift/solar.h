#pragma once

#include <QDate>
#include <QDateTime>

// Sunrise/sunset from the NOAA solar-position algorithm (the same maths behind
// redshift/wlsunset), so the schedule works offline once a location is known.
namespace solar {

struct DayEvents {
    enum class Kind {
        Normal,      // sunrise and sunset both happen on this date
        PolarDay,    // the sun never sets (high latitude summer)
        PolarNight,  // the sun never rises
    };
    Kind kind = Kind::Normal;
    QDateTime sunrise;  // local time; valid only for Kind::Normal
    QDateTime sunset;
};

// Civil sunrise/sunset (sun centre at -0.833°, i.e. upper limb on the horizon with
// refraction) for the given LOCAL date. Longitude is east-positive.
DayEvents dayEvents(const QDate &localDate, double latitudeDeg, double longitudeDeg);

} // namespace solar
