#pragma once

#include <QObject>
#include <QVariant>

class QQmlEngine;
class QJSValue;

// Off-the-GUI-thread JSON parsing, exposed to QML/JS as the global `JsonAsync` and
// wrapped in a Promise by qrc:/qbar/Json.js (`QJson.parse(text).then(...)`).
//
// QML's built-in JSON.parse is synchronous: a large payload (e.g. a 90-candle OHLC
// array) tokenizes on the GUI thread and blocks the event loop for the whole parse,
// which drops animation frames — the MPRIS marquee visibly stutters. JsonAsync runs
// QJsonDocument::fromJson on a QThreadPool worker and marshals the result back to the
// GUI thread, so only the (much cheaper) QVariant→JS conversion stays on the main loop.
//
// `JsonAsync.parse(text)` and `JsonAsync.stringify(value)` return a JsonReply.

class JsonReply final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    // Parsed successfully; `result` is the JS value (object/array/primitive).
    void finished(const QVariant &result);
    // Malformed JSON: QJsonParseError message + byte offset.
    void failed(const QString &error);
};

class JsonAsync final : public QObject {
    Q_OBJECT

public:
    explicit JsonAsync(QObject *parent = nullptr);

    // Install as the JS global `JsonAsync` on the engine. Once per engine, before QML.
    static void install(QQmlEngine *engine);

    Q_INVOKABLE JsonReply *parse(const QString &text);
    Q_INVOKABLE JsonReply *stringify(const QJSValue &value);
};
