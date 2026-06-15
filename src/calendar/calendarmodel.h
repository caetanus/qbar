#pragma once

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

#include <glib.h>

typedef struct _ICalTime ICalTime;
typedef struct _ICalComponent ICalComponent;
typedef struct _GCancellable GCancellable;
typedef struct _GError GError;

class CalendarModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QDate selectedDate READ selectedDate WRITE setSelectedDate NOTIFY selectedDateChanged)
    Q_PROPERTY(QVariantList selectedDayEvents READ selectedDayEvents NOTIFY selectedDayEventsChanged)
    Q_PROPERTY(QVariantList upcomingEvents READ upcomingEvents NOTIFY upcomingEventsChanged)
    Q_PROPERTY(QVariantMap eventCountsByDate READ eventCountsByDate NOTIFY eventCountsByDateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    struct EventItem {
        QString sourceLabel;
        QString title;
        QString location;
        QString url;
        QDateTime start;
        QDateTime end;
        bool allDay = false;
        bool cancelled = false;
    };

    explicit CalendarModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool loading() const { return m_loading; }
    QDate selectedDate() const { return m_selectedDate; }
    QVariantList selectedDayEvents() const { return m_selectedDayEvents; }
    QVariantList upcomingEvents() const { return m_upcomingEvents; }
    QVariantMap eventCountsByDate() const { return m_eventCountsByDate; }
    QString statusText() const { return m_statusText; }

    Q_INVOKABLE void refresh();
    void setSelectedDate(const QDate &date);

signals:
    void availableChanged();
    void loadingChanged();
    void selectedDateChanged();
    void selectedDayEventsChanged();
    void upcomingEventsChanged();
    void eventCountsByDateChanged();
    void statusTextChanged();

private:
    void startCacheRefresh();
    void startOnlineRefresh(bool showLoading);
    void applyCacheRefreshResult(int generation, bool available, const QString &statusText, QList<EventItem> events);
    void applyOnlineRefreshResult(int generation, bool available, const QString &statusText, QList<EventItem> events);
    void setAvailable(bool available);
    void setLoading(bool loading);
    void setStatusText(const QString &statusText);
    void rebuildDerivedData();
    QVariantList eventsToVariantList(const QList<EventItem> &events) const;
    QVariantMap eventCountsByDate(const QList<EventItem> &events) const;
    static QString formatRange(const EventItem &event);
    static bool eventMatchesSelectedDate(const EventItem &event, const QDate &date);
    static QDateTime toLocalDateTime(const ICalTime *timeValue);
    static gboolean collectInstance(ICalComponent *icomp,
                                    ICalTime *instance_start,
                                    ICalTime *instance_end,
                                    gpointer user_data,
                                    GCancellable *cancellable,
                                    GError **error);
    static QDateTime parseCacheTimestamp(const QString &value);
    static bool parseCacheIcalRange(const QString &rawObject, QDateTime *start, QDateTime *end, bool *allDay);
    static QString sourceLabelForUid(const QString &sourceUid);
    static QString cacheDbPathForUid(const QString &sourceUid);
    static QList<EventItem> loadCachedEventsForSource(const QString &sourceUid,
                                                     const QDateTime &windowStart,
                                                     const QDateTime &windowEnd);

    QList<EventItem> m_allEvents;
    QVariantList m_selectedDayEvents;
    QVariantList m_upcomingEvents;
    QVariantMap m_eventCountsByDate;
    QDate m_selectedDate;
    QString m_statusText;
    bool m_available = false;
    bool m_loading = false;
    int m_cacheRefreshGeneration = 0;
    int m_onlineRefreshGeneration = 0;
    bool m_cacheRefreshInFlight = false;
    bool m_onlineRefreshInFlight = false;
    bool m_onlineRefreshQueued = false;
    QTimer m_cacheRefreshTimer;
    QTimer m_onlineRefreshTimer;
};
