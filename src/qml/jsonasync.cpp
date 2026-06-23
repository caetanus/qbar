#include "jsonasync.h"

#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QQmlEngine>
#include <QThreadPool>

JsonAsync::JsonAsync(QObject *parent)
    : QObject(parent)
{
}

void JsonAsync::install(QQmlEngine *engine)
{
    if (engine == nullptr) {
        return;
    }
    auto *json = new JsonAsync(engine);
    const QJSValue wrapper = engine->newQObject(json);
    engine->globalObject().setProperty(QStringLiteral("JsonAsync"), wrapper);
}

JsonReply *JsonAsync::parse(const QString &text)
{
    auto *reply = new JsonReply(this);
    // Parent + explicit CppOwnership: the wrapper outlives the JS return and frees itself
    // once the parse settles, instead of being GC'd while the worker is still running.
    QQmlEngine::setObjectOwnership(reply, QQmlEngine::CppOwnership);

    // Copy the bytes onto the heap for the worker; toUtf8() must run here (QString is not
    // safe to touch from another thread once shared).
    const QByteArray utf8 = text.toUtf8();

    QThreadPool::globalInstance()->start([reply, utf8]() {
        // Worker thread: the expensive tokenization. QJsonDocument/QVariant don't touch
        // the QML engine, so building them off-thread is safe; only the emit is marshalled
        // back (the QVariant→JS conversion then happens on the GUI thread, in the slot).
        QJsonParseError err {};
        const QJsonDocument doc = QJsonDocument::fromJson(utf8, &err);
        if (err.error != QJsonParseError::NoError) {
            const QString msg = err.errorString() + QStringLiteral(" at offset ")
                + QString::number(err.offset);
            QMetaObject::invokeMethod(
                reply,
                [reply, msg]() {
                    emit reply->failed(msg);
                    reply->deleteLater();
                },
                Qt::QueuedConnection);
            return;
        }
        const QVariant result = doc.toVariant();
        QMetaObject::invokeMethod(
            reply,
            [reply, result]() {
                emit reply->finished(result);
                reply->deleteLater();
            },
            Qt::QueuedConnection);
    });

    return reply;
}

JsonReply *JsonAsync::stringify(const QJSValue &jsValue)
{
    auto *reply = new JsonReply(this);
    QQmlEngine::setObjectOwnership(reply, QQmlEngine::CppOwnership);
    // QML arrays/objects arrive as QJSValue. Convert while still on the engine thread;
    // the resulting detached QVariant tree is safe to serialize on the worker.
    const QVariant value = jsValue.toVariant();

    QThreadPool::globalInstance()->start([reply, value]() {
        const QJsonValue jsonValue = QJsonValue::fromVariant(value);
        QByteArray encoded;
        if (jsonValue.isObject()) {
            encoded = QJsonDocument(jsonValue.toObject()).toJson(QJsonDocument::Compact);
        } else if (jsonValue.isArray()) {
            encoded = QJsonDocument(jsonValue.toArray()).toJson(QJsonDocument::Compact);
        } else {
            // QJsonDocument only accepts an object/array root. Wrap primitives, encode,
            // then remove the brackets to preserve JSON.stringify-compatible output.
            encoded = QJsonDocument(QJsonArray{jsonValue}).toJson(QJsonDocument::Compact);
            if (encoded.size() >= 2)
                encoded = encoded.mid(1, encoded.size() - 2);
        }
        const QString result = QString::fromUtf8(encoded);
        QMetaObject::invokeMethod(reply, [reply, result]() {
            emit reply->finished(result);
            reply->deleteLater();
        }, Qt::QueuedConnection);
    });

    return reply;
}
