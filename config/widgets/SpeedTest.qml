import QtQuick
import QtCore
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch

// Speed-test widget — an EXTERNAL runtime widget (lives in ~/.config/qbar/widgets, NOT
// compiled into qbar). Click to open the popup and run an on-demand test.
//
// How it measures (like a normal speed test): it opens several parallel streams from
// Cloudflare's keyless endpoint (speed.cloudflare.com/__down) and TALLIES THE BYTES as they
// arrive — via NetReply's downloadProgress events (`Http.request(...).downloadProgress`) —
// then divides bytes by elapsed time. A short warm-up is discarded so TCP slow-start doesn't
// drag the figure down; the measurement window that follows is the reported throughput.
// Ping is the min round-trip of a few tiny requests. Upload isn't measured in this version.
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
    readonly property int concurrency: 4          // parallel streams (saturate the link)
    readonly property int chunkBytes: 10000000    // 10 MB download chunk; relaunched as they finish
    readonly property int uploadChunkBytes: 8000000 // 8 MB upload chunk
    readonly property int warmupMs: 1500          // discard TCP slow-start
    readonly property int durationMs: 8000        // each phase's length (warmup + window)
    readonly property int pingSamples: 5
    readonly property string downUrl: "https://speed.cloudflare.com/__down?bytes="
    readonly property string upUrl: "https://speed.cloudflare.com/__up"

    // ── public state (also read by the popup) ──
    property string phase: "idle"      // "idle" | "ping" | "download" | "upload" | "done" | "error"
    property real downloadMbps: 0      // final measured download throughput
    property real liveDownloadMbps: 0  // sampled instantaneous download rate
    property real uploadMbps: 0        // final measured upload throughput
    property real liveUploadMbps: 0    // sampled instantaneous upload rate
    property real pingMs: 0
    readonly property bool running: phase === "ping" || phase === "download" || phase === "upload"

    // ── internals (one tally engine, reused for the download then the upload phase) ──
    property real _bytes: 0            // cumulative bytes across all streams this phase
    property real _t0: 0
    property bool _active: false
    property var _replies: []
    property bool _measuring: false    // past warm-up → inside the measurement window
    property real _baseBytes: 0
    property real _baseT: 0
    property real _lastBytes: 0
    property real _lastT: 0
    property real _peak: 0             // max sampled rate this phase — the reported headline
    property string _uploadBody: ""    // generated once per upload phase, shared by chunks

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

    // Latency: min round-trip of a few zero-byte requests.
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
    function resetTally() {
        var now = Date.now()
        root._bytes = 0
        root._active = true
        root._measuring = false
        root._t0 = now
        root._baseBytes = 0
        root._baseT = now
        root._lastBytes = 0
        root._lastT = now
        root._peak = 0
        root._replies = []
    }

    function beginDownload() {
        root.phase = "download"
        resetTally()
        sampleTimer.start()
        for (var i = 0; i < root.concurrency; ++i)
            launchChunk("down")
    }

    function beginUpload() {
        // One shared body buffer for all the POSTs this phase (immutable string, reused).
        root._uploadBody = makeBody(root.uploadChunkBytes)
        root.phase = "upload"
        resetTally()
        sampleTimer.start()
        for (var i = 0; i < root.concurrency; ++i)
            launchChunk("up")
    }

    function makeBody(n) {
        return new Array(n + 1).join("x")  // n 'x' chars (V4 has String.repeat, but this is safe)
    }

    // Open a stream and tally its bytes as they move (download/uploadProgress report the
    // CUMULATIVE per-reply total, so we add the per-reply delta). When it finishes, relaunch
    // to keep the link busy until the window ends. We never read the body — only the tally.
    function launchChunk(kind) {
        if (!root._active)
            return
        var isUp = kind === "up"
        var url = isUp ? root.upUrl : (root.downUrl + root.chunkBytes)
        // discardBody: we measure via the progress signals and never read the payload, so the
        // transport throws the bytes away — otherwise the multi-MB bodies pile up (the JS
        // engine doesn't GC those large external buffers → qbar's RAM balloons across runs).
        var opts = isUp ? { method: "POST", body: root._uploadBody, timeout: 30000, discardBody: true,
                            headers: { "Content-Type": "application/octet-stream" } }
                        : { timeout: 30000, discardBody: true }
        var reply = Http.request(url, opts)
        root._replies.push(reply)
        var last = 0
        var onProgress = function (moved, total) {
            root._bytes += (moved - last)
            last = moved
        }
        if (isUp)
            reply.uploadProgress.connect(onProgress)
        else
            reply.downloadProgress.connect(onProgress)
        var done = function () {
            var idx = root._replies.indexOf(reply)
            if (idx >= 0)
                root._replies.splice(idx, 1)
            if (root._active && (Date.now() - root._t0) < root.durationMs)
                root.launchChunk(kind)
        }
        reply.finished.connect(done)
        reply.failed.connect(done)
    }

    function finishPhase(now) {
        root._active = false
        sampleTimer.stop()
        // Stop the in-flight tails so QNAM isn't left buffering chunks we won't use.
        for (var i = 0; i < root._replies.length; ++i) {
            if (root._replies[i])
                root._replies[i].abort()
        }
        root._replies = []
        var span = (now - (root._measuring ? root._baseT : root._t0)) / 1000
        var bytes = root._bytes - (root._measuring ? root._baseBytes : 0)
        var avg = span > 0 ? bytes * 8 / span / 1e6 : 0
        // Report the PEAK sustained rate (max sample within the measurement window), not the
        // last live reading nor the slow-start-dragged average — that's the headline figure.
        var result = Math.max(root._peak, avg)
        if (root.phase === "download") {
            root.downloadMbps = result
            root.liveDownloadMbps = 0
            beginUpload()  // chain into the upload phase
        } else {
            root.uploadMbps = result
            root.liveUploadMbps = 0
            root._uploadBody = ""  // free the buffer
            root.phase = "done"
        }
    }

    // Samples the running tally: computes the instantaneous rate for the live readout, opens
    // the measurement window after warm-up, and ends the phase at durationMs.
    Timer {
        id: sampleTimer
        interval: 200
        repeat: true
        running: false
        onTriggered: {
            if (!root._active) { stop(); return }
            var now = Date.now()
            var elapsed = now - root._t0

            if (!root._measuring && elapsed >= root.warmupMs) {
                root._measuring = true
                root._baseBytes = root._bytes
                root._baseT = now
            }

            var dt = (now - root._lastT) / 1000
            if (dt > 0) {
                var rate = (root._bytes - root._lastBytes) * 8 / dt / 1e6
                // Track the peak only within the measurement window (skip slow-start ramp).
                if (root._measuring && rate > root._peak)
                    root._peak = rate
                // Light smoothing so the live number doesn't jitter.
                if (root.phase === "upload")
                    root.liveUploadMbps = root.liveUploadMbps > 0 ? root.liveUploadMbps * 0.6 + rate * 0.4 : rate
                else
                    root.liveDownloadMbps = root.liveDownloadMbps > 0 ? root.liveDownloadMbps * 0.6 + rate * 0.4 : rate
            }
            root._lastBytes = root._bytes
            root._lastT = now

            if (elapsed >= root.durationMs)
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
