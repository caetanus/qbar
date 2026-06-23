#include "netfetch.h"

#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

#include <chrono>

NetReply::NetReply(QNetworkReply *reply, bool discardBody, QObject *parent)
    : QObject(parent)
    , m_reply(reply)
    , m_discardBody(discardBody)
{
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit downloadProgress(received, total); });
    connect(reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) { emit uploadProgress(sent, total); });
    if (m_discardBody) {
        // Throw the bytes away as they arrive so neither QNAM nor we ever hold the payload.
        connect(reply, &QNetworkReply::readyRead, this, [this]() {
            if (m_reply != nullptr) {
                m_reply->readAll();
            }
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (r == nullptr) {
            return;
        }
        if (r->error() != QNetworkReply::NoError) {
            // Emit BEFORE deleting: the JS handler reads the args synchronously here.
            emit failed(r->errorString());
        } else {
            const int status =
                r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit finished(status, m_discardBody ? QString() : QString::fromUtf8(r->readAll()));
        }
        r->deleteLater();
        deleteLater(); // one-shot: the JS Promise has settled, free the wrapper
    });
}

void NetReply::abort()
{
    if (m_reply != nullptr) {
        m_reply->abort(); // → finished() with OperationCanceledError → failed()
    }
}

NetFetch::NetFetch(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void NetFetch::install(QQmlEngine *engine)
{
    if (engine == nullptr) {
        return;
    }
    auto *http = new NetFetch(engine);
    const QJSValue wrapper = engine->newQObject(http);
    engine->globalObject().setProperty(QStringLiteral("Http"), wrapper);
}

NetReply *NetFetch::request(const QString &url, const QVariantMap &options)
{
    QNetworkRequest req{QUrl(url)};

    const int timeoutMs = options.value(QStringLiteral("timeout"), 15000).toInt();
    req.setTransferTimeout(std::chrono::milliseconds(timeoutMs));

    const QVariantMap headers = options.value(QStringLiteral("headers")).toMap();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
    }

    const QString method =
        options.value(QStringLiteral("method"), QStringLiteral("GET")).toString().toUpper();
    const QByteArray body = options.value(QStringLiteral("body")).toString().toUtf8();

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("GET")) {
        reply = m_nam->get(req);
    } else if (method == QLatin1String("POST")) {
        reply = m_nam->post(req, body);
    } else {
        reply = m_nam->sendCustomRequest(req, method.toUtf8(), body);
    }

    const bool discardBody = options.value(QStringLiteral("discardBody")).toBool();
    auto *netReply = new NetReply(reply, discardBody, this);
    // Parent + explicit CppOwnership: the wrapper outlives the JS return and frees itself
    // via deleteLater() once the request settles, instead of being GC'd mid-flight.
    QQmlEngine::setObjectOwnership(netReply, QQmlEngine::CppOwnership);
    return netReply;
}
