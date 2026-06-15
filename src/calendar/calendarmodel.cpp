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

QString textFromColumn(sqlite3_stmt *stmt, int column)
{
    const unsigned char *text = sqlite3_column_text(stmt, column);
    if (text == nullptr) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char *>(text));
}

QString firstMatch(const QString &value, const QRegularExpression &expression)
{
    const QRegularExpressionMatch match = expression.match(value);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
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

        QList<EventItem> events;
        int sourceCount = 0;

        const QDate today = QDate::currentDate();
        const QDate startMonth = QDate(today.year(), today.month(), 1).addMonths(-1);
        const QDate endMonth = startMonth.addMonths(3);
        const QDateTime startWindow(startMonth, QTime(0, 0), QTimeZone::systemTimeZone());
        const QDateTime endWindow(endMonth, QTime(0, 0), QTimeZone::systemTimeZone());

        QDirIterator it(QDir::homePath() + QStringLiteral("/.cache/evolution/calendar"),
                        QStringList() << QStringLiteral("cache.db"),
                        QDir::Files,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString dbPath = it.next();
            const QString sourceUid = QFileInfo(dbPath).dir().dirName();
            const QList<EventItem> sourceEvents = loadCachedEventsForSource(sourceUid, startWindow, endWindow);
            if (!sourceEvents.isEmpty()) {
                events.append(sourceEvents);
            }
            ++sourceCount;
        }

        std::sort(events.begin(), events.end(), [](const EventItem &a, const EventItem &b) {
            return a.start < b.start;
        });

        result.available = sourceCount > 0;
        result.statusText = sourceCount > 0
            ? QStringLiteral("%1 cached calendars, %2 events").arg(sourceCount).arg(events.size())
            : QStringLiteral("No cached calendar sources");
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

                CalendarLoadContext context;
                context.events = &events;
                context.sourceLabel = sourceLabel;

                e_cal_client_generate_instances_sync(E_CAL_CLIENT(client),
                                                     start,
                                                     end,
                                                     nullptr,
                                                     &CalendarModel::collectInstance,
                                                     &context);
                g_object_unref(client);
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

    if (m_onlineRefreshQueued) {
        m_onlineRefreshQueued = false;
        startOnlineRefresh(false);
    }
}

QDateTime CalendarModel::parseCacheTimestamp(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (trimmed.length() == 8) {
        const QDate date = QDate::fromString(trimmed, QStringLiteral("yyyyMMdd"));
        if (date.isValid()) {
            return QDateTime(date, QTime(0, 0), QTimeZone::systemTimeZone());
        }
    }

    if (trimmed.endsWith(QLatin1Char('Z')) && trimmed.length() == 16) {
        QDateTime dateTime = QDateTime::fromString(trimmed, QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
        if (dateTime.isValid()) {
            dateTime.setTimeZone(QTimeZone::utc());
            return dateTime.toLocalTime();
        }
    }

    if (trimmed.contains(QLatin1Char('T'))) {
        const QString token = trimmed.left(15);
        QDateTime dateTime = QDateTime::fromString(token, QStringLiteral("yyyyMMdd'T'HHmmss"));
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    if (trimmed.length() == 14) {
        QDateTime dateTime = QDateTime::fromString(trimmed, QStringLiteral("yyyyMMddHHmmss"));
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    return {};
}

bool CalendarModel::parseCacheIcalRange(const QString &rawObject, QDateTime *start, QDateTime *end, bool *allDay)
{
    const QRegularExpression startPattern(QStringLiteral(R"(DTSTART(?:;[^:\r\n]*)?:(\d{8}(?:T\d{6}Z?)?))"));
    const QRegularExpression endPattern(QStringLiteral(R"(DTEND(?:;[^:\r\n]*)?:(\d{8}(?:T\d{6}Z?)?))"));

    const QRegularExpressionMatch startMatch = startPattern.match(rawObject);
    if (startMatch.hasMatch()) {
        const QString token = startMatch.captured(1);
        if (allDay != nullptr) {
            *allDay = !token.contains(QLatin1Char('T'));
        }
        if (start != nullptr) {
            *start = parseCacheTimestamp(token);
        }
    }

    const QRegularExpressionMatch endMatch = endPattern.match(rawObject);
    if (endMatch.hasMatch() && end != nullptr) {
        *end = parseCacheTimestamp(endMatch.captured(1));
    }

    return start != nullptr && start->isValid();
}

QString CalendarModel::sourceLabelForUid(const QString &sourceUid)
{
    const QStringList searchRoots = {
        QDir::homePath() + QStringLiteral("/.cache/evolution/sources"),
        QDir::homePath() + QStringLiteral("/.config/evolution/sources"),
    };

    for (const QString &root : searchRoots) {
        QDirIterator it(root,
                        QStringList() << (sourceUid + QStringLiteral(".source")),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        if (!it.hasNext()) {
            continue;
        }

        const QString sourcePath = it.next();
        QSettings settings(sourcePath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Data Source"));
        QString label = settings.value(QStringLiteral("DisplayName")).toString().trimmed();
        settings.endGroup();

        if (label.isEmpty()) {
            settings.beginGroup(QStringLiteral("GNOME Online Accounts"));
            label = settings.value(QStringLiteral("Name")).toString().trimmed();
            settings.endGroup();
        }

        if (!label.isEmpty()) {
            return label;
        }
    }

    return sourceUid;
}

QString CalendarModel::cacheDbPathForUid(const QString &sourceUid)
{
    return QDir::homePath()
        + QStringLiteral("/.cache/evolution/calendar/")
        + sourceUid
        + QStringLiteral("/cache.db");
}

QList<CalendarModel::EventItem> CalendarModel::loadCachedEventsForSource(const QString &sourceUid,
                                                                         const QDateTime &windowStart,
                                                                         const QDateTime &windowEnd)
{
    QList<EventItem> events;
    const QString dbPath = cacheDbPathForUid(sourceUid);
    if (!QFileInfo::exists(dbPath)) {
        return events;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return events;
    }

    sqlite3_stmt *stmt = nullptr;
    static const char *sql = "SELECT summary, location, occur_start, occur_end, ECacheOBJ FROM ECacheObjects WHERE ECacheState = 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        sqlite3_close(db);
        return events;
    }

    const QString sourceLabel = sourceLabelForUid(sourceUid);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventItem event;
        event.sourceLabel = sourceLabel;
        event.title = textFromColumn(stmt, 0).trimmed();
        if (event.title.isEmpty()) {
            event.title = QStringLiteral("(untitled)");
        }
        event.location = textFromColumn(stmt, 1).trimmed();

        const QString occurStart = textFromColumn(stmt, 2).trimmed();
        const QString occurEnd = textFromColumn(stmt, 3).trimmed();
        const QString rawObject = textFromColumn(stmt, 4);

        event.start = parseCacheTimestamp(occurStart);
        event.end = parseCacheTimestamp(occurEnd);
        event.allDay = occurStart.length() == 8;

        if (!event.start.isValid() || !event.end.isValid()) {
            bool rawAllDay = false;
            QDateTime rawStart;
            QDateTime rawEnd;
            if (parseCacheIcalRange(rawObject, &rawStart, &rawEnd, &rawAllDay)) {
                if (!event.start.isValid()) {
                    event.start = rawStart;
                }
                if (!event.end.isValid()) {
                    event.end = rawEnd;
                }
                event.allDay = event.allDay || rawAllDay;
            }
        }

        event.cancelled = rawObject.contains(QStringLiteral("STATUS:CANCELLED"), Qt::CaseInsensitive)
            || rawObject.contains(QStringLiteral("STATUS;VALUE=TEXT:CANCELLED"), Qt::CaseInsensitive);
        event.url = firstMatch(rawObject, QRegularExpression(QStringLiteral(R"(URL:(https?://[^\r\n]+))")));

        if (!event.start.isValid()) {
            continue;
        }
        if (!event.end.isValid() || event.end < event.start) {
            event.end = event.start;
        }

        if (event.end < windowStart || event.start > windowEnd) {
            continue;
        }

        events.push_back(event);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return events;
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
