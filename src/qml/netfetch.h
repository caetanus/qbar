#pragma once

#include <QObject>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;

// A fetch() transport backed by QNetworkAccessManager, exposed to QML/JS as the global
// `Http`. QML's built-in XMLHttpRequest is a thin, lossy wrapper: it has no timeout,
// ontimeout, or onerror, and on a network drop it often never signals at all — the
// request just hangs. QNAM surfaces real errors (QNetworkReply::error/errorString), has
// a native transfer timeout, and follows redirects, so qrc:/qbar/Fetch.js builds its
// Promise wrapper on this instead.
//
// `Http.request(url, options) -> NetReply`. options: { method, headers, body, timeout }.

class NetReply final : public QObject {
    Q_OBJECT

public:
    // discardBody: drain the response incrementally and emit an empty body — for large
    // transfers (e.g. the speed test) whose body is never used. Avoids buffering the whole
    // payload in QNAM AND handing a huge QString to the JS engine, which doesn't count such
    // external allocations toward GC pressure, so they pile up (a 500 MB "leak" after a few
    // speed tests).
    explicit NetReply(QNetworkReply *reply, bool discardBody = false, QObject *parent = nullptr);

    Q_INVOKABLE void abort();

signals:
    // HTTP exchange completed (any status — check it in JS, like real fetch).
    void finished(int status, const QString &body);
    // Transport failure: DNS/connection/TLS error, timeout, or abort().
    void failed(const QString &error);
    // Streaming progress (forwards QNetworkReply::downloadProgress): bytes received so far
    // and the total (−1/0 if unknown). Lets JS measure throughput as data arrives — e.g.
    // the speed-test widget tallies bytes over a window instead of timing one big body.
    void downloadProgress(qint64 received, qint64 total);
    // Upload counterpart (forwards QNetworkReply::uploadProgress): bytes sent so far / total.
    // Lets the speed-test widget measure upload throughput by tallying a POST as it streams.
    void uploadProgress(qint64 sent, qint64 total);

private:
    QNetworkReply *m_reply = nullptr;
    bool m_discardBody = false;
};

class NetFetch final : public QObject {
    Q_OBJECT

public:
    explicit NetFetch(QObject *parent = nullptr);

    // Install as the JS global `Http` on the engine. Once per engine, before loading QML.
    static void install(QQmlEngine *engine);

    Q_INVOKABLE NetReply *request(const QString &url, const QVariantMap &options);

private:
    QNetworkAccessManager *m_nam = nullptr;
};
