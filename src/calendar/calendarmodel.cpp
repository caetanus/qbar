#include "calendarmodel.h"

#ifdef signals
#undef signals
#endif

#include <libedataserver/libedataserver.h>
#include <libecal/libecal.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QTimeZone>
#include <QThreadPool>
#include <QVariantMap>
#include <algorithm>

#include <sqlite3.h>

namespace {

struct CalendarLoadContext {
    QList<CalendarModel::EventItem> *events = nullptr;
    QString sourceLabel;
};

struct CalendarLoadResult {
    int generation = 0;
    bool available = false;
    QString statusText;
    QList<CalendarModel::EventItem> events;
    bool showLoading = false;
};

QString eventSummary(ICalComponent *icomp)
{
    const gchar *summary = i_cal_component_get_summary(icomp);
    if (summary != nullptr && *summary != '\0') {
        return QString::fromUtf8(summary);
    }
    return QStringLiteral("(untitled)");
}

QString eventLocation(ICalComponent *icomp)
{
    const gchar *location = i_cal_component_get_location(icomp);
    if (location != nullptr && *location != '\0') {
        return QString::fromUtf8(location);
    }
    return {};
}

} // namespace

CalendarModel::CalendarModel(QObject *parent)
    : QObject(parent)
    , m_selectedDate(QDate::currentDate())
{
    m_cacheRefreshTimer.setInterval(5 * 60 * 1000);
    m_cacheRefreshTimer.setSingleShot(false);
    connect(&m_cacheRefreshTimer, &QTimer::timeout, this, &CalendarModel::refresh);
    m_cacheRefreshTimer.start();

    m_onlineRefreshTimer.setSingleShot(true);
    m_onlineRefreshTimer.setInterval(2500);
    connect(&m_onlineRefreshTimer, &QTimer::timeout, this, [this]() {
        startOnlineRefresh(false);
    });

    QTimer::singleShot(0, this, &CalendarModel::refresh);
}

void CalendarModel::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    emit availableChanged();
}

void CalendarModel::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void CalendarModel::setStatusText(const QString &statusText)
{
    if (m_statusText == statusText) {
        return;
    }
    m_statusText = statusText;
    emit statusTextChanged();
}

void CalendarModel::setSelectedDate(const QDate &date)
{
    const QDate normalized = date.isValid() ? date : QDate::currentDate();
    if (m_selectedDate == normalized) {
        return;
    }
    m_selectedDate = normalized;
    emit selectedDateChanged();
    rebuildDerivedData();
}

void CalendarModel::startCacheRefresh()
{
    if (m_cacheRefreshInFlight) {
        return;
    }

    m_cacheRefreshInFlight = true;
    setLoading(true);
    setStatusText(QStringLiteral("Loading cached calendars"));

    const int generation = ++m_cacheRefreshGeneration;
    auto *task = QRunnable::create([guard = QPointer<CalendarModel>(this), generation]() {
        CalendarLoadResult result;
        result.generation = generation;

        // Read from our own sqlite cache (populated by the last online refresh).
        QList<EventItem> events = loadEventsFromCache();
        std::sort(events.begin(), events.end(), [](const EventItem &a, const EventItem &b) {
            return a.start < b.start;
        });

        result.available = !events.isEmpty();
        result.statusText = result.available
            ? QStringLiteral("%1 cached events").arg(events.size())
            : QStringLiteral("No cached calendar data");
        result.events = std::move(events);

        if (!guard) {
            return;
        }

        QMetaObject::invokeMethod(guard,
                                  [guard, result = std::move(result)]() mutable {
                                      if (!guard) {
                                          return;
                                      }
                                      guard->applyCacheRefreshResult(result.generation,
                                                                      result.available,
                                                                      result.statusText,
                                                                      std::move(result.events));
                                  },
                                  Qt::QueuedConnection);
    });
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}

void CalendarModel::startOnlineRefresh(bool showLoading)
{
    if (m_onlineRefreshInFlight) {
        m_onlineRefreshQueued = true;
        return;
    }

    m_onlineRefreshInFlight = true;
    if (showLoading) {
        setLoading(true);
        setStatusText(QStringLiteral("Loading calendars"));
    }

    const int generation = ++m_onlineRefreshGeneration;
    auto *task = QRunnable::create([guard = QPointer<CalendarModel>(this), generation, showLoading]() {
        CalendarLoadResult result;
        result.generation = generation;
        result.showLoading = showLoading;

        QList<EventItem> events;
        GError *error = nullptr;
        ESourceRegistry *registry = e_source_registry_new_sync(nullptr, &error);
        if (registry == nullptr) {
            qWarning() << "[calendar] registry load failed:" << (error != nullptr ? error->message : "unknown error");
            if (error != nullptr) {
                g_error_free(error);
            }
            result.available = false;
            result.statusText = QStringLiteral("Calendar backend unavailable");
        } else {
            GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
            int sourceCount = 0;

            const QDateTime now = QDateTime::currentDateTime();
            const QDateTime startWindow = now.addYears(-1);
            const QDateTime endWindow = now.addYears(2);
            const time_t start = static_cast<time_t>(startWindow.toSecsSinceEpoch());
            const time_t end = static_cast<time_t>(endWindow.toSecsSinceEpoch());

            // Two passes. qbar only ever READS the EDS local store; with no other
            // EDS consumer running (Evolution closed), nothing pokes the backends
            // to hit the network, so events created elsewhere never arrive — the
            // calendar "stops syncing" after the first load. Pass 1 connects and
            // REQUESTS a backend refresh (async on the EDS side); after a short
            // settle, pass 2 reads. Anything the server delivers later than the
            // settle window is picked up by the next periodic cycle.
            struct ConnectedSource {
                EClient *client;
                QString label;
                bool refreshRequested;
            };
            QVector<ConnectedSource> connected;

            for (GList *link = sources; link != nullptr; link = link->next) {
                auto *source = static_cast<ESource *>(link->data);
                if (source == nullptr) {
                    continue;
                }

                ++sourceCount;
                const gchar *displayName = e_source_get_display_name(source);
                QString sourceLabel = displayName != nullptr ? QString::fromUtf8(displayName) : QString();
                if (sourceLabel.isEmpty()) {
                    sourceLabel = QStringLiteral("Calendar");
                }

                if (auto *goa = E_SOURCE_GOA(e_source_get_extension(source, E_SOURCE_EXTENSION_GOA)); goa != nullptr) {
                    const gchar *address = e_source_goa_get_address(goa);
                    if (address != nullptr && *address != '\0') {
                        sourceLabel = QString::fromUtf8(address);
                    }
                }

                EClient *client = e_cal_client_connect_sync(source,
                                                            E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
                                                            30,
                                                            nullptr,
                                                            &error);
                if (client == nullptr) {
                    qWarning() << "[calendar] connect failed for" << sourceLabel << (error != nullptr ? error->message : "unknown error");
                    if (error != nullptr) {
                        g_error_free(error);
                        error = nullptr;
                    }
                    continue;
                }

                bool refreshRequested = false;
                if (e_client_check_refresh_supported(client)) {
                    if (e_client_refresh_sync(client, nullptr, &error)) {
                        refreshRequested = true;
                    } else {
                        qWarning() << "[calendar] refresh request failed for" << sourceLabel
                                   << (error != nullptr ? error->message : "unknown error");
                        if (error != nullptr) {
                            g_error_free(error);
                            error = nullptr;
                        }
                    }
                }
                connected.append(ConnectedSource{client, sourceLabel, refreshRequested});
            }

            // Give the backends a moment to pull from their servers before reading.
            // Worker thread — blocking here never touches the GUI.
            bool anyRefresh = false;
            for (const ConnectedSource &cs : connected) {
                anyRefresh = anyRefresh || cs.refreshRequested;
            }
            if (anyRefresh) {
                g_usleep(3 * G_USEC_PER_SEC);
            }

            for (const ConnectedSource &cs : connected) {
                CalendarLoadContext context;
                context.events = &events;
                context.sourceLabel = cs.label;

                e_cal_client_generate_instances_sync(E_CAL_CLIENT(cs.client),
                                                     start,
                                                     end,
                                                     nullptr,
                                                     &CalendarModel::collectInstance,
                                                     &context);
                g_object_unref(cs.client);
            }

            if (sources != nullptr) {
                g_list_free_full(sources, g_object_unref);
            }
            g_object_unref(registry);

            std::sort(events.begin(), events.end(), [](const EventItem &a, const EventItem &b) {
                return a.start < b.start;
            });

            result.available = sourceCount > 0;
            result.statusText = sourceCount > 0
                ? QStringLiteral("%1 calendars, %2 events").arg(sourceCount).arg(events.size())
                : QStringLiteral("No calendar sources");
        }

        result.events = std::move(events);

        if (!guard) {
            return;
        }

        QMetaObject::invokeMethod(guard,
                                  [guard, result = std::move(result)]() mutable {
                                      if (!guard) {
                                          return;
                                      }
                                      guard->applyOnlineRefreshResult(result.generation,
                                                                      result.available,
                                                                      result.statusText,
                                                                      std::move(result.events));
                                  },
                                  Qt::QueuedConnection);
    });
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}

void CalendarModel::applyCacheRefreshResult(int generation, bool available, const QString &statusText, QList<EventItem> events)
{
    if (generation != m_cacheRefreshGeneration) {
        return;
    }

    m_allEvents = std::move(events);
    setAvailable(available);
    setStatusText(statusText);
    setLoading(false);
    m_cacheRefreshInFlight = false;
    rebuildDerivedData();

    if (available) {
        if (!m_onlineRefreshTimer.isActive()) {
            m_onlineRefreshTimer.start();
        }
    } else {
        startOnlineRefresh(true);
    }
}

void CalendarModel::applyOnlineRefreshResult(int generation, bool available, const QString &statusText, QList<EventItem> events)
{
    if (generation != m_onlineRefreshGeneration) {
        return;
    }

    m_allEvents = std::move(events);
    setAvailable(available);
    setStatusText(statusText);
    setLoading(false);
    m_onlineRefreshInFlight = false;
    rebuildDerivedData();

    if (available) {
        // Persist to our own sqlite cache off the GUI thread for the next startup.
        const QList<EventItem> snapshot = m_allEvents;
        QThreadPool::globalInstance()->start(QRunnable::create([snapshot]() {
            persistEventsToCache(snapshot);
        }));
    }

    if (m_onlineRefreshQueued) {
        m_onlineRefreshQueued = false;
        startOnlineRefresh(false);
    }
}

QString CalendarModel::ourCacheDbPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(base).filePath(QStringLiteral("qbar/calendar.db"));
}

QList<CalendarModel::EventItem> CalendarModel::loadEventsFromCache()
{
    QList<EventItem> events;
    const QString path = ourCacheDbPath();
    if (!QFileInfo::exists(path)) {
        return events;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return events;
    }

    sqlite3_stmt *stmt = nullptr;
    static const char *sql =
        "SELECT source_label, title, location, url, start_secs, end_secs, all_day, cancelled FROM events";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK && stmt != nullptr) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EventItem event;
            event.sourceLabel = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            event.title = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            event.location = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
            event.url = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
            event.start = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(stmt, 4));
            event.end = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(stmt, 5));
            event.allDay = sqlite3_column_int(stmt, 6) != 0;
            event.cancelled = sqlite3_column_int(stmt, 7) != 0;
            events.append(event);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return events;
}

void CalendarModel::persistEventsToCache(const QList<EventItem> &events)
{
    const QString path = ourCacheDbPath();
    QDir().mkpath(QFileInfo(path).path());

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return;
    }

    sqlite3_exec(db,
                 "CREATE TABLE IF NOT EXISTS events ("
                 "source_label TEXT, title TEXT, location TEXT, url TEXT,"
                 "start_secs INTEGER, end_secs INTEGER, all_day INTEGER, cancelled INTEGER)",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM events", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    sqlite3_stmt *stmt = nullptr;
    static const char *sql =
        "INSERT INTO events (source_label, title, location, url, start_secs, end_secs, all_day, cancelled)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK && stmt != nullptr) {
        for (const EventItem &event : events) {
            const QByteArray label = event.sourceLabel.toUtf8();
            const QByteArray title = event.title.toUtf8();
            const QByteArray location = event.location.toUtf8();
            const QByteArray url = event.url.toUtf8();
            sqlite3_bind_text(stmt, 1, label.constData(), label.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, title.constData(), title.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, location.constData(), location.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, url.constData(), url.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, event.start.isValid() ? event.start.toSecsSinceEpoch() : 0);
            sqlite3_bind_int64(stmt, 6, event.end.isValid() ? event.end.toSecsSinceEpoch() : 0);
            sqlite3_bind_int(stmt, 7, event.allDay ? 1 : 0);
            sqlite3_bind_int(stmt, 8, event.cancelled ? 1 : 0);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

QDateTime CalendarModel::toLocalDateTime(const ICalTime *timeValue)
{
    if (timeValue == nullptr || !i_cal_time_is_valid_time(timeValue)) {
        return {};
    }

    const time_t secs = i_cal_time_as_timet_with_zone(timeValue, e_cal_util_get_system_timezone());
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs), QTimeZone::systemTimeZone());
}

gboolean CalendarModel::collectInstance(ICalComponent *icomp,
                                        ICalTime *instance_start,
                                        ICalTime *instance_end,
                                        gpointer user_data,
                                        GCancellable *cancellable,
                                        GError **error)
{
    Q_UNUSED(cancellable)
    Q_UNUSED(error)

    auto *context = static_cast<CalendarLoadContext *>(user_data);
    if (context == nullptr || context->events == nullptr || icomp == nullptr || instance_start == nullptr) {
        return TRUE;
    }

    EventItem event;
    event.sourceLabel = context->sourceLabel;
    event.title = eventSummary(icomp);
    event.location = eventLocation(icomp);
    event.start = toLocalDateTime(instance_start);
    event.end = instance_end != nullptr ? toLocalDateTime(instance_end) : event.start;
    event.allDay = i_cal_time_is_date(instance_start);
    event.cancelled = i_cal_component_get_status(icomp) == I_CAL_STATUS_CANCELLED;

    if (ICalComponent *cloned = i_cal_component_clone(icomp)) {
        ECalComponent *component = e_cal_component_new_from_icalcomponent(cloned);
        if (component != nullptr) {
            gchar *url = e_cal_component_get_url(component);
            if (url != nullptr && *url != '\0') {
                event.url = QString::fromUtf8(url);
            }
            g_free(url);
            g_object_unref(component);
        } else {
            i_cal_component_free(cloned);
        }
    }

    if (!event.start.isValid()) {
        return TRUE;
    }
    if (!event.end.isValid() || event.end < event.start) {
        event.end = event.start;
    }

    context->events->append(event);
    return TRUE;
}

QString CalendarModel::formatRange(const EventItem &event)
{
    if (event.allDay) {
        return QStringLiteral("All day");
    }

    return event.start.toLocalTime().toString(QStringLiteral("HH:mm"))
        + QStringLiteral(" - ")
        + event.end.toLocalTime().toString(QStringLiteral("HH:mm"));
}

bool CalendarModel::eventMatchesSelectedDate(const EventItem &event, const QDate &date)
{
    if (!date.isValid() || !event.start.isValid()) {
        return false;
    }

    const QDate startDate = event.start.date();
    const QDate endDate = event.end.isValid() ? event.end.date() : startDate;
    return startDate <= date && endDate >= date;
}

QVariantList CalendarModel::eventsToVariantList(const QList<EventItem> &events) const
{
    QVariantList list;
    list.reserve(events.size());
    for (const EventItem &event : events) {
        QVariantMap item;
        item.insert(QStringLiteral("title"), event.title);
        item.insert(QStringLiteral("location"), event.location);
        item.insert(QStringLiteral("sourceLabel"), event.sourceLabel);
        item.insert(QStringLiteral("startText"), event.allDay ? QStringLiteral("All day") : event.start.toString(QStringLiteral("HH:mm")));
        item.insert(QStringLiteral("rangeText"), formatRange(event));
        item.insert(QStringLiteral("dateText"), event.start.date().toString(QStringLiteral("ddd, d MMM")));
        item.insert(QStringLiteral("dayText"), event.start.date().toString(QStringLiteral("ddd, d MMM")));
        item.insert(QStringLiteral("url"), event.url);
        item.insert(QStringLiteral("cancelled"), event.cancelled);
        item.insert(QStringLiteral("allDay"), event.allDay);
        item.insert(QStringLiteral("start"), event.start);
        item.insert(QStringLiteral("end"), event.end);
        list.push_back(item);
    }
    return list;
}

QVariantMap CalendarModel::eventCountsByDate(const QList<EventItem> &events) const
{
    QVariantMap counts;
    for (const EventItem &event : events) {
        if (!event.start.isValid()) {
            continue;
        }

        QDate startDate = event.start.date();
        QDate endDate = event.end.isValid() ? event.end.date() : startDate;
        if (event.allDay && event.end.isValid() && event.end.time() == QTime(0, 0) && endDate > startDate) {
            endDate = endDate.addDays(-1);
        }
        if (endDate < startDate) {
            endDate = startDate;
        }

        for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
            const QString key = date.toString(QStringLiteral("yyyy-MM-dd"));
            counts.insert(key, counts.value(key).toInt() + 1);
        }
    }
    return counts;
}

void CalendarModel::rebuildDerivedData()
{
    QList<EventItem> selectedEvents;
    QList<EventItem> upcomingEvents;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime upcomingCutoff = now.addDays(7);

    for (const EventItem &event : std::as_const(m_allEvents)) {
        if (eventMatchesSelectedDate(event, m_selectedDate)) {
            selectedEvents.push_back(event);
        }
        if (event.start.isValid() && event.end.isValid() && event.end >= now && event.start <= upcomingCutoff) {
            upcomingEvents.push_back(event);
        }
    }

    std::sort(selectedEvents.begin(), selectedEvents.end(), [](const EventItem &a, const EventItem &b) {
        return a.start < b.start;
    });
    std::sort(upcomingEvents.begin(), upcomingEvents.end(), [](const EventItem &a, const EventItem &b) {
        return a.start < b.start;
    });

    if (selectedEvents.isEmpty()) {
        for (const EventItem &event : std::as_const(m_allEvents)) {
            if (event.start.isValid() && event.start.date() == m_selectedDate) {
                selectedEvents.push_back(event);
            }
        }
        std::sort(selectedEvents.begin(), selectedEvents.end(), [](const EventItem &a, const EventItem &b) {
            return a.start < b.start;
        });
    }

    if (upcomingEvents.size() > 6) {
        upcomingEvents.erase(upcomingEvents.begin() + 6, upcomingEvents.end());
    }

    m_selectedDayEvents = eventsToVariantList(selectedEvents);
    m_upcomingEvents = eventsToVariantList(upcomingEvents);
    const QVariantMap counts = eventCountsByDate(m_allEvents);
    if (m_eventCountsByDate != counts) {
        m_eventCountsByDate = counts;
        emit eventCountsByDateChanged();
    }
    emit selectedDayEventsChanged();
    emit upcomingEventsChanged();
}

void CalendarModel::refresh()
{
    startCacheRefresh();
}
