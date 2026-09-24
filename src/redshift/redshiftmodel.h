#pragma once

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>
#include <QTimer>
#include <QVariantMap>

#include "solar.h"

class GammaBackend;
class QNetworkAccessManager;

// Screen colour temperature by time of day (a redshift/wlsunset replacement): warm at
// night, neutral by day, with a linear ramp around sunrise and sunset.
//
// Location, in order of preference: explicit latitude/longitude; a `city` geocoded via
// open-meteo (cached on disk, so it works offline after the first hit); the Weather
// widget's location, which config.cpp copies in when the redshift block has none; and
// finally the fixed `sunrise`/`sunset` clock times. Sun times come from the NOAA
// algorithm in solar.cpp — nothing is fetched for them.
//
// Config keys (all optional): enabled, day (K), night (K), transition (minutes, the
// whole ramp), latitude, longitude, city, label, sunrise/sunset ("HH:mm").
class RedshiftModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY availabilityChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool manual READ manual NOTIFY stateChanged)
    Q_PROPERTY(int temperature READ temperature NOTIFY stateChanged)
    Q_PROPERTY(int dayTemperature READ dayTemperature NOTIFY stateChanged)
    Q_PROPERTY(int nightTemperature READ nightTemperature NOTIFY stateChanged)
    // 0 = at (or above) the day temperature, 1 = at (or below) the night one.
    Q_PROPERTY(double warmth READ warmth NOTIFY stateChanged)
    // The applied whitepoint as a colour — what "white" looks like right now.
    Q_PROPERTY(QColor tint READ tint NOTIFY stateChanged)
    // "day" | "night" | "sunrise" | "sunset" (the two ramps).
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString sunrise READ sunrise NOTIFY stateChanged)
    Q_PROPERTY(QString sunset READ sunset NOTIFY stateChanged)
    Q_PROPERTY(QString location READ location NOTIFY stateChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY stateChanged)

public:
    explicit RedshiftModel(QObject *parent = nullptr);
    ~RedshiftModel() override;

    // Idempotent: the same map is a no-op, so a config reload never resets the user's
    // toggle/manual state unless the block actually changed.
    void setConfig(const QVariantMap &config);

    bool available() const;
    QString backendName() const;
    bool enabled() const;
    bool manual() const;
    int temperature() const;
    int dayTemperature() const;
    int nightTemperature() const;
    double warmth() const;
    QColor tint() const;
    QString phase() const;
    QString sunrise() const;
    QString sunset() const;
    QString location() const;
    QString tooltipText() const;

    Q_INVOKABLE void toggle();
    void setEnabled(bool enabled);
    // Manual override: shift the current temperature by deltaKelvin and hold it until
    // resetAuto(). Enables the model if it was off.
    Q_INVOKABLE void nudge(int deltaKelvin);
    // Manual override at an absolute temperature (the menu presets).
    Q_INVOKABLE void setTemperature(int kelvin);
    Q_INVOKABLE void resetAuto();

signals:
    void availabilityChanged();
    void stateChanged();

private:
    struct Schedule {
        double sunriseMinutes = 0;  // minutes after local midnight
        double sunsetMinutes = 0;
        solar::DayEvents::Kind kind = solar::DayEvents::Kind::Normal;
        bool fromSun = false;
    };

    void tick();
    Schedule scheduleFor(const QDate &localDate);
    int scheduledTemperature(const QDateTime &now, const Schedule &schedule, QString *phase) const;
    void applyTemperature(int kelvin);
    void resolveLocation();
    void requestGeocode(const QString &city);
    bool loadGeocodeCache(const QString &city, double *lat, double *lon, QString *name) const;
    void storeGeocodeCache(const QString &city, double lat, double lon, const QString &name) const;
    QString geocodeCachePath() const;
    void setLocation(double lat, double lon, const QString &name);

    QVariantMap m_config;
    bool m_configured = false;
    GammaBackend *m_backend = nullptr;
    QTimer m_timer;
    QNetworkAccessManager *m_network = nullptr;

    int m_dayTemp = 6500;
    int m_nightTemp = 4000;
    int m_transitionMinutes = 60;
    QTime m_fixedSunrise{6, 30};
    QTime m_fixedSunset{18, 30};
    bool m_hasLocation = false;
    double m_latitude = 0;
    double m_longitude = 0;
    QString m_locationName;
    QString m_locationSource;   // "" or the customTools key the location was borrowed from
    QString m_pendingCity;      // geocode in flight / to retry
    int m_geocodeAttempts = 0;

    bool m_enabled = true;
    bool m_manual = false;
    int m_manualTemp = 6500;
    int m_temperature = 6500;
    int m_appliedTemp = -1;     // last value pushed to the backend; -1 = released
    QString m_phase;
    QDate m_scheduleDate;
    Schedule m_schedule;
};
