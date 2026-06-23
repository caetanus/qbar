.pragma library
.import "qrc:/qbar/Json.js" as QJson

// A minimal WHATWG-fetch shim for QML, backed by the C++ `Http` global (QNetworkAccessManager).
//
// We do NOT use QML's XMLHttpRequest: it has no timeout/ontimeout/onerror and on a network
// drop it frequently never signals at all — the request just hangs (and hung requests
// exhaust the per-host connection pool, freezing the bar). `Http` (see src/qml/netfetch.cpp)
// surfaces real transport errors and has a native transfer timeout.
//
// Usage reads like the standard Fetch API:
//
//   Fetch.fetch(url).then(r => r.ok ? r.json() : Promise.reject(r.status)).then(data => …)
//
// Like real fetch, it resolves (not rejects) on HTTP error statuses — check `r.ok` /
// `r.status` — and rejects only on network/transport failure or timeout.
// options: { method, headers, body, timeout (ms, default 15000) }.
//
// (Qt's V4 engine supports ES6+ but NOT async/await, hence the Promise.then style.)
function fetch(url, options = {}) {
    return new Promise((resolve, reject) => {
        const reply = Http.request(url, options)

        // Optional streaming progress: options.onProgress(received, total) fires as bytes
        // arrive (backed by NetReply::downloadProgress) — for progress bars / throughput.
        if (typeof options.onProgress === "function") {
            reply.downloadProgress.connect(options.onProgress)
        }

        reply.finished.connect((status, body) => resolve({
            ok: status >= 200 && status < 300,
            status,
            text: () => Promise.resolve(body),
            // Parse off the GUI thread (worker-threaded QJsonDocument) so a large body
            // doesn't block the event loop and stutter animations like the MPRIS marquee.
            json: () => QJson.parse(body)
        }))

        reply.failed.connect(error => reject(new Error(error)))
    })
}
