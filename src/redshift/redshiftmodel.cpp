#include "redshiftmodel.h"

#include "colortemp.h"
#include "gammabackend.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTickMs = 30 * 1000;
constexpr int kGeocodeRetryMs = 5 * 60 * 1000;

int clampKelvin(int k)
{
    return std::clamp(k, colortemp::kMinKelvin, colortemp::kMaxKelvin);
}

QTime parseClock(const QVariant &value, const QTime &fallback)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return fallback;
    }
    QTime t = QTime::fromString(text, QStringLiteral("HH:mm"));
    if (!t.isValid()) {
        t = QTime::fromString(text, QStringLiteral("H:mm"));
    }
    return t.isValid() ? t : fallback;
}

bool numeric(const QVariant &v, double *out)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (ok && std::isfinite(d)) {
        *out = d;
    }
    return ok && std::isfinite(d);
}

QString clockText(const QTime &t)
{
    return QLocale::system().toString(t, QLocale::ShortFormat);
}

} // namespace

RedshiftModel::RedshiftModel(QObject *parent)
    : QObject(parent)
{
    m_backend = createGammaBackend(this);
    if (m_backend != nullptr) {
        connect(m_backend, &GammaBackend::availabilityChanged, this, [this] {
            // A new output inherits the current temperature (apply() re-pushes).
            if (m_enabled && m_appliedTemp >= 0) {
                m_backend->apply(colortemp::whitepoint(m_appliedTemp));
            }
            emit availabilityChanged();
            emit stateChanged();
        });
        qInfo() << "[redshift] gamma backend:" << m_backend->name() << m_backend->status();
    } else {
        qWarning() << "[redshift] no gamma backend for platform — the applet will be inert";
    }
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &RedshiftModel::tick);
    m_timer.start();
}

RedshiftModel::~RedshiftModel()
{
    if (m_backend != nullptr) {
        m_backend->release();
    }
}

void RedshiftModel::setConfig(const QVariantMap &config)
{
    if (m_configured && config == m_config) {
        return;
    }
    const bool first = !m_configured;
    m_config = config;
    m_configured = true;

    m_dayTemp = clampKelvin(config.value(QStringLiteral("day"), 6500).toInt());
    m_nightTemp = clampKelvin(config.value(QStringLiteral("night"), 4000).toInt());
    m_transitionMinutes = std::max(1, config.value(QStringLiteral("transition"), 60).toInt());
    m_fixedSunrise = parseClock(config.value(QStringLiteral("sunrise")), QTime(6, 30));
    m_fixedSunset = parseClock(config.value(QStringLiteral("sunset")), QTime(18, 30));
    m_locationSource = config.value(QStringLiteral("locationSource")).toString();
    if (first) {
        m_enabled = config.value(QStringLiteral("enabled"), true).toBool();
        m_manualTemp = m_dayTemp;
    }

    resolveLocation();
    m_scheduleDate = QDate();  // force a schedule rebuild
    tick();
}

void RedshiftModel::resolveLocation()
{
    m_hasLocation = false;
    m_locationName.clear();
    m_pendingCity.clear();
    m_geocodeAttempts = 0;

    double lat = 0;
    double lon = 0;
    const bool hasLat = numeric(m_config.value(QStringLiteral("latitude")), &lat);
    const bool hasLon = numeric(m_config.value(QStringLiteral("longitude")), &lon);
    const QString city = m_config.value(QStringLiteral("city")).toString().trimmed();
    const QString label = m_config.value(QStringLiteral("label")).toString().trimmed();

    if (hasLat && hasLon) {
        QString name = !label.isEmpty() ? label : city;
        if (name.isEmpty()) {
            name = QStringLiteral("%1, %2").arg(lat, 0, 'f', 2).arg(lon, 0, 'f', 2);
        }
        setLocation(lat, lon, name);
        return;
    }
    if (city.isEmpty()) {
        return;  // fixed clock times
    }
    QString cachedName;
    if (loadGeocodeCache(city, &lat, &lon, &cachedName)) {
        setLocation(lat, lon, !label.isEmpty() ? label : cachedName);
        return;
    }
    m_locationName = !label.isEmpty() ? label : city;
    m_pendingCity = city;
    requestGeocode(city);
}

void RedshiftModel::setLocation(double lat, double lon, const QString &name)
{
    m_hasLocation = std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0;
    m_latitude = lat;
    m_longitude = lon;
    m_locationName = name;
    m_scheduleDate = QDate();
}

QString RedshiftModel::geocodeCachePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/qbar/redshift-geocode.json");
}

bool RedshiftModel::loadGeocodeCache(const QString &city, double *lat, double *lon, QString *name) const
{
    QFile file(geocodeCachePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject entry = root.value(city.toLower()).toObject();
    if (entry.isEmpty()) {
        return false;
    }
    *lat = entry.value(QStringLiteral("latitude")).toDouble();
    *lon = entry.value(QStringLiteral("longitude")).toDouble();
    *name = entry.value(QStringLiteral("name")).toString(city);
    return std::isfinite(*lat) && std::isfinite(*lon);
}

void RedshiftModel::storeGeocodeCache(const QString &city, double lat, double lon, const QString &name) const
{
    const QString path = geocodeCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(in.readAll()).object();
        }
    }
    root.insert(city.toLower(), QJsonObject{
        {QStringLiteral("latitude"), lat},
        {QStringLiteral("longitude"), lon},
        {QStringLiteral("name"), name},
    });
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

void RedshiftModel::requestGeocode(const QString &city)
{
    if (m_network == nullptr) {
        m_network = new QNetworkAccessManager(this);
    }
    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), city);
    query.addQueryItem(QStringLiteral("count"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    ++m_geocodeAttempts;
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, city] {
        reply->deleteLater();
        if (m_pendingCity != city) {
            return;  // config changed meanwhile
        }
        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject hit = root.value(QStringLiteral("results")).toArray().first().toObject();
            const double lat = hit.value(QStringLiteral("latitude")).toDouble(NAN);
            const double lon = hit.value(QStringLiteral("longitude")).toDouble(NAN);
            if (std::isfinite(lat) && std::isfinite(lon)) {
                const QString name = hit.value(QStringLiteral("name")).toString(city);
                storeGeocodeCache(city, lat, lon, name);
                const QString label = m_config.value(QStringLiteral("label")).toString().trimmed();
                setLocation(lat, lon, !label.isEmpty() ? label : name);
                m_pendingCity.clear();
                ok = true;
                qInfo() << "[redshift] geocoded" << city << "->" << lat << lon;
            }
        }
        if (!ok) {
            qWarning() << "[redshift] geocoding" << city << "failed:"
                       << (reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("no result"))
                       << "- retrying in 5 min (fixed clock times until then)";
            QTimer::singleShot(kGeocodeRetryMs, this, [this, city] {
                if (m_pendingCity == city) {
                    requestGeocode(city);
                }
            });
        }
        tick();
    });
}

RedshiftModel::Schedule RedshiftModel::scheduleFor(const QDate &localDate)
{
    Schedule s;
    if (m_hasLocation) {
        const solar::DayEvents ev = solar::dayEvents(localDate, m_latitude, m_longitude);
        s.kind = ev.kind;
        s.fromSun = true;
        if (ev.kind == solar::DayEvents::Kind::Normal) {
            const QDateTime midnight = localDate.startOfDay();
            s.sunriseMinutes = midnight.msecsTo(ev.sunrise) / 60000.0;
            s.sunsetMinutes = midnight.msecsTo(ev.sunset) / 60000.0;
        }
        return s;
    }
    s.sunriseMinutes = m_fixedSunrise.msecsSinceStartOfDay() / 60000.0;
    s.sunsetMinutes = m_fixedSunset.msecsSinceStartOfDay() / 60000.0;
    return s;
}

int RedshiftModel::scheduledTemperature(const QDateTime &now, const Schedule &schedule, QString *phase) const
{
    if (schedule.kind == solar::DayEvents::Kind::PolarDay) {
        *phase = QStringLiteral("day");
        return m_dayTemp;
    }
    if (schedule.kind == solar::DayEvents::Kind::PolarNight) {
        *phase = QStringLiteral("night");
        return m_nightTemp;
    }
    const double t = now.time().msecsSinceStartOfDay() / 60000.0;
    const double half = m_transitionMinutes / 2.0;
    const auto lerp = [](int from, int to, double f) {
        return static_cast<int>(std::lround(from + (to - from) * std::clamp(f, 0.0, 1.0)));
    };
    if (t < schedule.sunriseMinutes - half) {
        *phase = QStringLiteral("night");
        return m_nightTemp;
    }
    if (t < schedule.sunriseMinutes + half) {
        *phase = QStringLiteral("sunrise");
        return lerp(m_nightTemp, m_dayTemp, (t - (schedule.sunriseMinutes - half)) / m_transitionMinutes);
    }
    if (t < schedule.sunsetMinutes - half) {
        *phase = QStringLiteral("day");
        return m_dayTemp;
    }
    if (t < schedule.sunsetMinutes + half) {
        *phase = QStringLiteral("sunset");
        return lerp(m_dayTemp, m_nightTemp, (t - (schedule.sunsetMinutes - half)) / m_transitionMinutes);
    }
    *phase = QStringLiteral("night");
    return m_nightTemp;
}

void RedshiftModel::tick()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (m_scheduleDate != now.date()) {
        m_scheduleDate = now.date();
        m_schedule = scheduleFor(m_scheduleDate);
    }
    QString phase;
    int target = scheduledTemperature(now, m_schedule, &phase);
    if (m_manual) {
        target = m_manualTemp;
    }
    if (!m_enabled) {
        target = colortemp::kNeutralKelvin;
    }

    const bool changed = phase != m_phase || target != m_temperature;
    m_phase = phase;
    m_temperature = target;

    if (m_backend != nullptr) {
        if (m_enabled) {
            if (m_appliedTemp != target) {
                m_backend->apply(colortemp::whitepoint(target));
                m_appliedTemp = target;
            }
        } else if (m_appliedTemp >= 0) {
            m_backend->release();
            m_appliedTemp = -1;
        }
    }
    if (changed) {
        emit stateChanged();
    }
}

bool RedshiftModel::available() const
{
    return m_backend != nullptr && m_backend->available();
}

QString RedshiftModel::backendName() const
{
    return m_backend != nullptr ? m_backend->name() : QString();
}

bool RedshiftModel::enabled() const
{
    return m_enabled;
}

bool RedshiftModel::manual() const
{
    return m_manual;
}

int RedshiftModel::temperature() const
{
    return m_temperature;
}

int RedshiftModel::dayTemperature() const
{
    return m_dayTemp;
}

int RedshiftModel::nightTemperature() const
{
    return m_nightTemp;
}

double RedshiftModel::warmth() const
{
    if (m_dayTemp == m_nightTemp) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(m_dayTemp - m_temperature) / (m_dayTemp - m_nightTemp), 0.0, 1.0);
}

QColor RedshiftModel::tint() const
{
    const colortemp::Whitepoint wp = colortemp::whitepoint(m_temperature);
    return QColor::fromRgbF(static_cast<float>(wp.r), static_cast<float>(wp.g), static_cast<float>(wp.b));
}

QString RedshiftModel::phase() const
{
    return m_phase;
}

QString RedshiftModel::sunrise() const
{
    if (m_schedule.kind != solar::DayEvents::Kind::Normal) {
        return {};
    }
    return clockText(QTime::fromMSecsSinceStartOfDay(static_cast<int>(std::lround(m_schedule.sunriseMinutes * 60000.0))));
}

QString RedshiftModel::sunset() const
{
    if (m_schedule.kind != solar::DayEvents::Kind::Normal) {
        return {};
    }
    return clockText(QTime::fromMSecsSinceStartOfDay(static_cast<int>(std::lround(m_schedule.sunsetMinutes * 60000.0))));
}

QString RedshiftModel::location() const
{
    return m_locationName;
}

QString RedshiftModel::tooltipText() const
{
    if (m_backend == nullptr) {
        return tr("Redshift: no gamma control on this platform");
    }
    QStringList lines;
    if (m_enabled) {
        lines << tr("%1 K · %2").arg(m_temperature).arg(m_manual ? tr("manual") : tr("auto"));
    } else {
        lines << tr("Redshift off");
    }
    if (m_schedule.kind == solar::DayEvents::Kind::PolarDay) {
        lines << tr("Polar day — no sunset");
    } else if (m_schedule.kind == solar::DayEvents::Kind::PolarNight) {
        lines << tr("Polar night — no sunrise");
    } else if (m_schedule.fromSun) {
        lines << tr("Sunrise %1 · Sunset %2").arg(sunrise(), sunset());
    } else {
        lines << tr("Fixed schedule %1 · %2").arg(sunrise(), sunset());
    }
    if (!m_locationName.isEmpty()) {
        QString where = m_locationName;
        if (!m_pendingCity.isEmpty()) {
            where = tr("Locating %1…").arg(m_locationName);
        } else if (!m_locationSource.isEmpty()) {
            where = tr("%1 (from the weather widget)").arg(m_locationName);
        }
        lines << where;
    }
    if (!available()) {
        lines << tr("No output under control: %1").arg(m_backend->status());
    }
    return lines.join(QLatin1Char('\n'));
}

void RedshiftModel::toggle()
{
    setEnabled(!m_enabled);
}

void RedshiftModel::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    tick();
    emit stateChanged();
}

void RedshiftModel::nudge(int deltaKelvin)
{
    if (!m_manual) {
        m_manualTemp = m_enabled ? m_temperature : m_dayTemp;
    }
    m_manual = true;
    m_manualTemp = clampKelvin(m_manualTemp + deltaKelvin);
    m_enabled = true;
    tick();
    emit stateChanged();
}

void RedshiftModel::setTemperature(int kelvin)
{
    m_manual = true;
    m_manualTemp = clampKelvin(kelvin);
    m_enabled = true;
    tick();
    emit stateChanged();
}

void RedshiftModel::resetAuto()
{
    if (!m_manual) {
        return;
    }
    m_manual = false;
    tick();
    emit stateChanged();
}
