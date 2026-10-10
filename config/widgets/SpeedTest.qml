import QtQuick
import QtCore
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch

// Speed-test widget — an EXTERNAL runtime widget (lives in ~/.config/qbar/widgets, NOT
// compiled into qbar). Click to open the popup and run an on-demand test.
//
// How it measures (like a normal speed test): it opens several parallel streams against
// Cloudflare's keyless endpoint (speed.cloudflare.com/__down and __up) and tallies the bytes
// that move. Download bytes are credited as they arrive (NetReply.downloadProgress); upload
// bytes only when the server has acknowledged a whole chunk, since socket-level upload
// progress is not throughput. The live number is the rate over a sliding window; the
// headline is the pruned mean of the window samples taken after a warm-up (fastest 10% and
// slowest 30% discarded), and a phase ends as soon as that rate settles. Ping is the min
// round-trip of a few tiny requests.
//
// Enable in ~/.config/qbar/config.json:
//   "modules-right": [ "CustomTool:custom/speedtest" ],
//   "customTools": { "custom/speedtest": { "source": "widgets/SpeedTest.qml" } }
QBar.CssRect {
    id: root

    property string toolId: ""
    cssId: "custom-speedtest"
    height: theme.height
    // Sizing contract: the bar's Loader has anchors.fill and resizes the item, which CLEARS
    // a `width:` binding — so Bar.appletWidth reads `preferredWidth`. Drive that, not width.
    property int preferredWidth: Math.max(1, content.implicitWidth + 14)
    width: Math.max(1, preferredWidth)

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

    // ── test parameters ──
    readonly property int concurrency: 6           // parallel streams (saturate the link)
    readonly property int chunkBytes: 50000000     // 50 MB download chunk; a replacement starts at 70%
    readonly property int warmupMs: 2000           // discard TCP slow-start
    readonly property int minMeasureMs: 4000       // measurement window: at least this long…
    readonly property int maxPhaseMs: 14000         // …and the whole phase never longer than this
    readonly property int tickMs: 100
    readonly property int pingSamples: 5
    readonly property string downUrl: "https://speed.cloudflare.com/__down?bytes="
    readonly property string upUrl: "https://speed.cloudflare.com/__up"

    // ── public state (also read by the popup) ──
    property string phase: "idle"      // "idle" | "ping" | "download" | "upload" | "done" | "error"
    property real downloadMbps: 0      // final measured download throughput
    property real liveDownloadMbps: 0  // sliding-window download rate while measuring
    property real uploadMbps: 0        // final measured upload throughput
    property real liveUploadMbps: 0    // sliding-window upload rate while measuring
    property real pingMs: 0
    readonly property bool running: phase === "ping" || phase === "download" || phase === "upload"

    // ── internals (one tally engine, reused for the download then the upload phase) ──
    //
    // Why a sliding window: the old "bytes since the last 200 ms tick" readout went to ZERO
    // whenever no progress event happened to land inside a tick (QNAM reports per readyRead,
    // the GUI thread batches events) and every time the synchronized chunks all ended at once.
    // The rate is now bytes moved over the last `windowMs`, read off a timeline of samples.
    property real _bytes: 0            // cumulative CREDITED bytes across all streams this phase
    property real _t0: 0
    property bool _active: false
    property var _replies: []
    property var _timeline: []         // [{t, b}] cumulative tally per tick (kept ~3 s deep)
    property bool _measuring: false    // past warm-up → inside the measurement window
    property real _baseT: 0
    property var _samples: []          // window rates collected while measuring → the headline
    property int _windowMs: 1000
    property int _upChunk: 1000000     // adaptive upload chunk (bytes)
    property var _bodies: ({})         // upload bodies by size, generated once and reused
    property int _gen: 0               // phase generation: late callbacks of an aborted phase must not relaunch

    // Last results persisted so the bar shows something before the first run.
    Settings {
        category: "speedTest"
        property alias downloadMbps: root.downloadMbps
        property alias uploadMbps: root.uploadMbps
        property alias pingMs: root.pingMs
    }

    function startTest() {
        if (running)
            return
        root.pingMs = 0
        root.downloadMbps = 0
        root.liveDownloadMbps = 0
        root.uploadMbps = 0
        root.liveUploadMbps = 0
        root.phase = "ping"
        measurePing(root.pingSamples, [])
    }

    // Latency: min round-trip of a few zero-byte requests (the first one also pays the TLS
    // handshake — taking the minimum discards it).
    function measurePing(remaining, samples) {
        if (remaining <= 0) {
            var best = Infinity
            for (var i = 0; i < samples.length; ++i)
                best = Math.min(best, samples[i])
            root.pingMs = (best === Infinity) ? 0 : Math.round(best)
            beginDownload()
            return
        }
        var t = Date.now()
        Fetch.fetch(root.downUrl + "0", { timeout: 5000 })
            .then(function () { samples.push(Date.now() - t); root.measurePing(remaining - 1, samples) })
            .catch(function () { root.measurePing(remaining - 1, samples) })
    }

    // Reset the tally engine for a fresh phase.
    function resetTally(windowMs) {
        var now = Date.now()
        root._bytes = 0
        root._active = true
        root._measuring = false
        root._t0 = now
        root._baseT = now
        root._timeline = [{ t: now, b: 0 }]
        root._samples = []
        root._windowMs = windowMs
        root._replies = []
        root._gen += 1
    }

    function beginDownload() {
        root.phase = "download"
        resetTally(1000)
        sampleTimer.start()
        for (var i = 0; i < root.concurrency; ++i)
            launchDown()
    }

    function beginUpload() {
        root.phase = "upload"
        root._upChunk = 1000000
        // Upload bytes are credited per COMPLETED chunk (see launchUp), so the window is a
        // little wider to average over several completions.
        resetTally(2000)
        sampleTimer.start()
        for (var i = 0; i < root.concurrency; ++i)
            launchUp()
    }

    function body(n) {
        if (!root._bodies[n])
            root._bodies[n] = "x".repeat(n)
        return root._bodies[n]
    }

    function dropReply(reply) {
        var idx = root._replies.indexOf(reply)
        if (idx >= 0)
            root._replies.splice(idx, 1)
    }

    // Download stream: tally bytes as they arrive (downloadProgress is cumulative per reply,
    // so credit the delta). At 70% of the chunk a REPLACEMENT stream is opened, so the link
    // never idles between chunks — the old scheme launched the next chunk only when the
    // previous one had fully ended, and with all streams in lockstep that left a gap of
    // nothing at every rotation. We never read the body (discardBody), only the tally.
    function launchDown() {
        if (!root._active)
            return
        var gen = root._gen
        var reply = Http.request(root.downUrl + root.chunkBytes, { timeout: 60000, discardBody: true })
        root._replies.push(reply)
        var last = 0
        var replaced = false
        reply.downloadProgress.connect(function (moved, total) {
            root._bytes += (moved - last)
            last = moved
            if (gen === root._gen && !replaced && total > 0 && moved >= total * 0.7 && root._replies.length < root.concurrency + 2) {
                replaced = true
                root.launchDown()
            }
        })
        var done = function () {
            if (gen !== root._gen)
                return  // the phase that owned this stream is over (abort tail)
            root.dropReply(reply)
            if (root._active && !replaced)
                root.launchDown()
        }
        reply.finished.connect(done)
        reply.failed.connect(done)
    }

    // Upload stream: bytes are credited only when the server has ACKED the whole chunk
    // (`finished`). QNAM's uploadProgress counts bytes handed to the socket, and the kernel's
    // send buffer swallows megabytes instantly — that is what made the old live upload shoot
    // up and then sit at 0 while the buffer drained. Smallish chunks keep completions frequent;
    // the size adapts so each one lasts ~0.5 s per stream at the current rate.
    function launchUp() {
        if (!root._active)
            return
        var size = root._upChunk
        var gen = root._gen
        var reply = Http.request(root.upUrl, { method: "POST", body: body(size), timeout: 60000, discardBody: true,
                                              headers: { "Content-Type": "application/octet-stream" } })
        root._replies.push(reply)
        reply.finished.connect(function (status) {
            if (gen !== root._gen)
                return
            root.dropReply(reply)
            if (status >= 200 && status < 400)
                root._bytes += size
            root.adaptUploadChunk()
            if (root._active)
                root.launchUp()
        })
        reply.failed.connect(function (err) {
            if (gen !== root._gen)
                return
            root.dropReply(reply)
            if (!root._active)
                return  // our own abort() at the end of the phase
            console.warn("speedtest: upload chunk failed:", err)
            root.launchUp()
        })
    }

    function adaptUploadChunk() {
        var mbps = root.liveUploadMbps
        if (mbps <= 0)
            return
        var perStreamBytesPerSec = mbps * 1e6 / 8 / root.concurrency
        var target = perStreamBytesPerSec * 0.5
        var size = 262144
        while (size * 2 <= target && size < 8388608)
            size *= 2
        root._upChunk = size
    }

    // Rate over the trailing window, read off the timeline. 0 until the window has depth.
    function windowRate(now) {
        var tl = root._timeline
        var oldest = null
        for (var i = tl.length - 1; i >= 0; --i) {
            if (now - tl[i].t >= root._windowMs) { oldest = tl[i]; break }
        }
        if (!oldest)
            oldest = tl[0]
        var span = (now - oldest.t) / 1000
        if (span < root._windowMs / 1000 * 0.5)
            return -1
        return (root._bytes - oldest.b) * 8 / span / 1e6
    }

    // Ookla-style pruning: drop the fastest 10% and the slowest 30% of the window samples,
    // average the rest. A spike or a stall inside a 100 ms tick no longer moves the headline.
    function prunedMean(samples) {
        if (samples.length === 0)
            return 0
        var sorted = samples.slice().sort(function (a, b) { return a - b })
        var lo = Math.floor(sorted.length * 0.3)
        var hi = Math.ceil(sorted.length * 0.9)
        if (hi - lo < 1) { lo = 0; hi = sorted.length }
        var sum = 0
        for (var i = lo; i < hi; ++i)
            sum += sorted[i]
        return sum / (hi - lo)
    }

    // Stop early once the last 3 s of samples sit within ±8% of their mean.
    function stable() {
        var n = Math.round(3000 / root.tickMs)
        var s = root._samples
        if (s.length < n)
            return false
        var min = Infinity, max = -Infinity, sum = 0
        for (var i = s.length - n; i < s.length; ++i) {
            min = Math.min(min, s[i]); max = Math.max(max, s[i]); sum += s[i]
        }
        var mean = sum / n
        return mean > 0 && (max - min) / mean < 0.16
    }

    function finishPhase(now) {
        root._active = false
        sampleTimer.stop()
        // Stop the in-flight tails so QNAM isn't left moving bytes we won't use.
        for (var i = 0; i < root._replies.length; ++i) {
            if (root._replies[i])
                root._replies[i].abort()
        }
        root._replies = []
        var result = root.prunedMean(root._samples)
        if (result <= 0) {
            // Window never stabilised (phase cut short): fall back to the plain average.
            var span = (now - root._t0) / 1000
            result = span > 0 ? root._bytes * 8 / span / 1e6 : 0
        }
        console.info("speedtest:", root.phase, result.toFixed(1), "Mbps from", root._samples.length,
                     "samples over", ((now - root._t0) / 1000).toFixed(1), "s")
        if (root.phase === "download") {
            root.downloadMbps = result
            root.liveDownloadMbps = 0
            // Chain into the upload phase after a short gap so the aborted download
            // streams finish tearing down before the POSTs claim the connections.
            phaseGap.start()
        } else {
            root.uploadMbps = result
            root.liveUploadMbps = 0
            root._bodies = ({})  // free the buffers
            root.phase = "done"
        }
    }

    Timer { id: phaseGap; interval: 500; repeat: false; onTriggered: root.beginUpload() }

    // Every tick: extend the timeline, refresh the live readout from the sliding window,
    // open the measurement window after warm-up, collect samples, and end the phase when
    // the rate has settled (or at maxPhaseMs).
    Timer {
        id: sampleTimer
        interval: root.tickMs
        repeat: true
        running: false
        onTriggered: {
            if (!root._active) { stop(); return }
            var now = Date.now()
            var elapsed = now - root._t0

            root._timeline.push({ t: now, b: root._bytes })
            while (root._timeline.length > 1 && now - root._timeline[0].t > root._windowMs + 1000)
                root._timeline.shift()

            if (!root._measuring && elapsed >= root.warmupMs) {
                root._measuring = true
                root._baseT = now
            }

            var rate = root.windowRate(now)
            if (rate >= 0) {
                if (root.phase === "upload")
                    root.liveUploadMbps = rate
                else
                    root.liveDownloadMbps = rate
                if (root._measuring)
                    root._samples.push(rate)
            }

            var measured = now - root._baseT
            if (elapsed >= root.maxPhaseMs || (root._measuring && measured >= root.minMeasureMs && root.stable()))
                root.finishPhase(now)
        }
    }

    Component.onCompleted: {
        preferredWidthUpdated(preferredWidth)
        // Expose custom IPC actions so a sway keybind can run a test:
        //   bindsym $mod+s       exec qbar-ipc trigger speedtest-run        (silent — bar only)
        //   bindsym $mod+Shift+s exec qbar-ipc trigger speedtest-run-popup  (also opens the popup)
        if (typeof qbarIpc !== "undefined" && qbarIpc) {
            qbarIpc.registerCommand("speedtest-run", function () {
                root.startTest()
            })
            qbarIpc.registerCommand("speedtest-run-popup", function () {
                popup.open()
                root.startTest()
            })
        }
    }

    function fmtMbps(v) {
        if (v <= 0) return "—"
        return v >= 100 ? v.toFixed(0) : v.toFixed(1)
    }

    // theme.foreground is a HexArgb STRING — parse it before reading .r/.g/.b (else → black).
    readonly property color fg: (cssTheme && cssTheme.loaded) ? cssTheme.parseColor(theme.foreground) : "#cccccc"

    // Reset the pulse when a run ends (the SequentialAnimation leaves opacity mid-fade).
    onRunningChanged: if (!running) glyph.opacity = 1.0

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 5

        Text {
            id: glyph
            anchors.verticalCenter: parent.verticalCenter
            text: root.phase === "upload" ? "↑" : (root.phase === "download" ? "↓" : "↯")
            color: root.running ? root.fg : Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.7)
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
            font.bold: true
            // Render-thread pulse (OpacityAnimator, not GUI-thread NumberAnimation): while a
            // test runs it keeps the otherwise-idle bar SURFACE presenting frames, so the live
            // number repaints even with the popup closed. (A GUI-thread animation marks the
            // scene dirty but doesn't drive continuous frames on an idle Wayland surface — the
            // same reason the MPRIS marquee uses render-thread animators.)
            SequentialAnimation {
                running: root.running
                loops: Animation.Infinite
                OpacityAnimator { target: glyph; from: 1.0; to: 0.4; duration: 600; easing.type: Easing.InOutQuad }
                OpacityAnimator { target: glyph; from: 0.4; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
            }
        }
        QBar.CssText {
            cssId: "custom-speedtest"
            anchors.verticalCenter: parent.verticalCenter
            // Never run yet → just "speedtest" (no "— Mbps"); otherwise the live/peak rate.
            text: root.running
                ? root.fmtMbps(root.phase === "upload" ? root.liveUploadMbps : root.liveDownloadMbps)
                : (root.downloadMbps > 0 ? root.fmtMbps(root.downloadMbps) : "speedtest")
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.running || root.downloadMbps > 0   // hidden until there's a result
            text: "Mbps"
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.6)
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize - 2
        }
    }

    QBar.Popup {
        id: popup
        name: "speedtest"   // reachable via the IPC: `qbar-ipc toggle speedtest`
        anchorItem: root
        source: Qt.resolvedUrl("SpeedTestPopup.qml")
        payload: ({ engine: root })
        popupWidth: 300
        popupHeight: 295
        gap: 4
        placement: "below"
        horizontalAlignment: "center"
    }

    // Tooltip: the bar label only shows download, so surface down + up + ping on hover.
    property bool tooltipHovered: false
    function tooltipText() {
        if (root.running)
            return "Speed test running…"
        if (root.downloadMbps <= 0)
            return "Click to run a speed test"
        var t = "↓ " + root.fmtMbps(root.downloadMbps) + " Mbps    ↑ " + root.fmtMbps(root.uploadMbps) + " Mbps"
        if (root.pingMs > 0)
            t += "    ·    ping " + root.pingMs + " ms"
        return t
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered && !popup.isOpen
        text: root.tooltipText()
        side: "auto"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: popup.toggle()
    }
}
