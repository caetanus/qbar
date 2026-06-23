.pragma library

// Async JSON.parse backed by the C++ `JsonAsync` global (see src/qml/jsonasync.cpp),
// which parses on a QThreadPool worker thread and resolves on the GUI thread. Use this
// instead of the synchronous JSON.parse for non-trivial payloads (e.g. OHLC candle
// arrays) so a big parse doesn't block the event loop and stall animations — the MPRIS
// marquee was visibly stuttering ("travadinhas") on each synchronous parse.
//
//   import "qrc:/qbar/Json.js" as QJson
//   QJson.parse(bodyText).then(obj => …).catch(err => …)
//
// (Qt's V4 engine has Promise but not async/await, hence the .then style.)
function parse(text) {
    return new Promise((resolve, reject) => {
        const reply = JsonAsync.parse(text)
        reply.finished.connect(result => resolve(result))
        reply.failed.connect(error => reject(new Error(error)))
    })
}

// Async JSON.stringify using the same worker-backed transport as parse().
function stringify(value) {
    return new Promise((resolve, reject) => {
        const reply = JsonAsync.stringify(value)
        reply.finished.connect(result => resolve(result))
        reply.failed.connect(error => reject(new Error(error)))
    })
}
