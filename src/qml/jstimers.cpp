#include "jstimers.h"

#include <QJSEngine>
#include <QTimer>

JsTimers::JsTimers(QObject *parent)
    : QObject(parent)
{
}

void JsTimers::install(QJSEngine *engine)
{
    if (engine == nullptr) {
        return;
    }
    auto *timers = new JsTimers(engine);
    const QJSValue wrapper = engine->newQObject(timers);
    QJSValue global = engine->globalObject();
    // Extracting a method off the wrapper keeps the QObject bound as its receiver, so the
    // globals call back into this instance.
    global.setProperty(QStringLiteral("setTimeout"), wrapper.property(QStringLiteral("setTimeout")));
    global.setProperty(QStringLiteral("setInterval"), wrapper.property(QStringLiteral("setInterval")));
    global.setProperty(QStringLiteral("clearTimeout"), wrapper.property(QStringLiteral("clearTimeout")));
    global.setProperty(QStringLiteral("clearInterval"), wrapper.property(QStringLiteral("clearInterval")));
}

int JsTimers::start(const QJSValue &callback, int delay, bool repeat)
{
    if (!callback.isCallable()) {
        return 0;
    }

    const int id = m_nextId++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(!repeat);
    m_timers.insert(id, timer);

    connect(timer, &QTimer::timeout, this, [this, id, callback, repeat]() {
        // One-shots free their id/timer before firing so a re-entrant clearTimeout(id)
        // from inside the callback is a harmless no-op rather than a double-free.
        if (!repeat) {
            if (QTimer *t = m_timers.take(id)) {
                t->deleteLater();
            }
        }
        const QJSValue result = callback.call();
        if (result.isError()) {
            qWarning("JsTimers: callback threw: %s", qPrintable(result.toString()));
        }
    });

    timer->start(delay < 0 ? 0 : delay);
    return id;
}

int JsTimers::setTimeout(const QJSValue &callback, int delay)
{
    return start(callback, delay, /*repeat=*/false);
}

int JsTimers::setInterval(const QJSValue &callback, int delay)
{
    return start(callback, delay, /*repeat=*/true);
}

void JsTimers::clearTimeout(int id)
{
    if (QTimer *t = m_timers.take(id)) {
        t->stop();
        t->deleteLater();
    }
}

void JsTimers::clearInterval(int id)
{
    clearTimeout(id); // one id space; either clearer cancels either kind
}
