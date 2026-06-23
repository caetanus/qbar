#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>

class QJSEngine;
class QTimer;

// Injects Web-style setTimeout / setInterval / clearTimeout / clearInterval globals into
// a QML/JS engine, backed by QTimer. Qt's V4 engine ships none of these (nor globalThis),
// so any timer-driven JS — e.g. the request timeout in qrc:/qbar/Fetch.js — has no native
// primitive to build on otherwise.
//
// Lifetime: install() parents the helper to the engine, so it (and its timers) die with
// the engine. Call install() once per engine, before loading QML that relies on it.
class JsTimers final : public QObject {
    Q_OBJECT

public:
    explicit JsTimers(QObject *parent = nullptr);

    static void install(QJSEngine *engine);

    Q_INVOKABLE int setTimeout(const QJSValue &callback, int delay);
    Q_INVOKABLE int setInterval(const QJSValue &callback, int delay);
    Q_INVOKABLE void clearTimeout(int id);
    Q_INVOKABLE void clearInterval(int id);

private:
    int start(const QJSValue &callback, int delay, bool repeat);

    QHash<int, QTimer *> m_timers;
    int m_nextId = 1;
};
