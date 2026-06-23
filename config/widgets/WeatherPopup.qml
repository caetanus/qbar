import QtQuick
import "qrc:/qbar/Fetch.js" as Fetch
import "Wmo.js" as Wmo

// Forecast popup for the Weather widget — EXTERNAL (loaded from disk by Weather.qml).
// Deliberately heavy: current conditions, N configurable metrics, an animated 24h
// temperature chart (Canvas) with precipitation-probability bars, and a 7-day forecast
// with animated min–max range bars. It doubles as a stress test for the runtime-widget +
// Fetch + animation stack.
Item {
    id: root

    // Set from Weather.qml's Popup.payload.
    property real lat: NaN
    property real lon: NaN
    property string locationName: ""
    property var metrics: ["temperature_2m", "apparent_temperature", "relative_humidity_2m", "wind_speed_10m", "uv_index"]
    property bool animatedBackground: true

    // Theme (HexArgb strings — fine for QML `color`; for Canvas use rgba() below).
    // Prefer the CSS #popup color so the text obeys the active theme (e.g. white on the
    // bliss-xp blue popup); fall back to the config foreground only when #popup sets none.
    readonly property var popupStyle: (typeof cssTheme !== "undefined" && cssTheme && cssTheme.loaded)
        ? cssTheme.resolve("popup") : ({})
    readonly property string fg: popupStyle["color"] ? popupStyle["color"]
        : ((typeof theme !== "undefined" && theme.foreground) ? theme.foreground : "#e6e6e6")
    readonly property string accent: (typeof theme !== "undefined" && theme.accent) ? theme.accent : "#63b3ed"
    readonly property string fontFamily: (typeof theme !== "undefined" && theme.fontFamily) ? theme.fontFamily : "Sans Serif"
    // Canvas-safe family for ctx.font: Context2D rejects the "Sans Serif" alias, so map aliases
    // to CSS generics (unquoted) it accepts; quote a real family. (See BitcoinPopup for the why.)
    function canvasFontFamily() {
        var l = root.fontFamily.toLowerCase()
        if (l === "sans serif" || l === "sansserif" || l === "sans-serif") return "sans-serif"
        if (l === "serif") return "serif"
        if (l === "monospace" || l === "mono") return "monospace"
        if (l === "cursive" || l === "fantasy") return l
        return '"' + root.fontFamily + '"'
    }
    readonly property int fontSize: (typeof theme !== "undefined" && theme.fontSize) ? theme.fontSize : 10
    readonly property string warm: "#e8954a"
    readonly property string cool: "#5aa9e6"
    // The chart hover tooltip has a fixed dark background, so its text must stay light —
    // `fg` follows the theme and is dark on a light popup (would be black-on-black).
    readonly property string tipFg: "#eef2f7"

    // Live data.
    property var current: ({})
    property var units: ({})
    property var hours: []   // [{ label, temp, pp }] — next 24h
    property var days: []    // [{ date, code, tmax, tmin }]
    property real tMin: 0
    property real tMax: 1
    property real dMin: 0
    property real dMax: 1
    property bool loading: true
    property bool failed: false

    // Seeded from the payload (the bar already has a current code) so the backdrop can
    // animate immediately; refined when the forecast request returns.
    property int code: -1
    property bool isDay: true
    readonly property var wx: code >= 0 ? Wmo.describe(code, isDay) : ({ emoji: "…", label: "" })

    // Animation drivers.
    property real chartProgress: 0
    property real barsProgress: 0

    implicitWidth: 360
    implicitHeight: 540

    // For Canvas (ctx.fillStyle/strokeStyle accept the CSS functional form).
    function rgba(colStr, alphaMul = 1) {
        const c = Qt.color(colStr)
        const a = c.a * alphaMul
        return `rgba(${Math.round(c.r * 255)},${Math.round(c.g * 255)},${Math.round(c.b * 255)},${a.toFixed(3)})`
    }

    // For QML `color:` properties — QML does NOT parse the CSS "rgba()" string (it falls
    // back to black), so return a real QML color via Qt.rgba.
    function tint(colStr, alphaMul = 1) {
        const c = Qt.color(colStr)
        return Qt.rgba(c.r, c.g, c.b, c.a * alphaMul)
    }

    // One line of the chart hover tooltip.
    component TipText: Text {
        font.family: root.fontFamily
        font.pointSize: root.fontSize - 2
    }

    function fmt(key) {
        const v = current[key]
        if (v === undefined || v === null)
            return "—"
        const num = (typeof v === "number") ? (Math.round(v * 10) / 10) : v
        const u = units[key] ?? ""
        return u ? `${num} ${u}` : `${num}`
    }

    function loadAll() {
        if (isNaN(root.lat)) {
            root.failed = true
            root.loading = false
            return
        }
        root.loading = true
        root.failed = false

        const curVars = ["weather_code", "temperature_2m", "is_day"]
        for (const m of root.metrics)
            if (!curVars.includes(m))
                curVars.push(m)

        const url = "https://api.open-meteo.com/v1/forecast?timezone=auto&forecast_days=7"
                + `&current=${curVars.join(",")}`
                + "&hourly=temperature_2m,precipitation_probability"
                + "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                + `&latitude=${root.lat}&longitude=${root.lon}`

        Fetch.fetch(url)
            .then(r => r.json())
            .then(d => {
                root.current = d.current
                root.units = d.current_units
                if (d.current.weather_code !== undefined)
                    root.code = d.current.weather_code
                if (d.current.is_day !== undefined)
                    root.isDay = d.current.is_day === 1

                // Next 24h, anchored at the current hour. current.time carries minutes
                // (e.g. ...T22:15) while hourly.time is top-of-hour (...T22:00), so match
                // on the "YYYY-MM-DDTHH" prefix rather than the full string.
                const hourKey = d.current.time.substring(0, 13)
                let start = d.hourly.time.findIndex(t => t.substring(0, 13) === hourKey)
                if (start < 0)
                    start = 0

                const hh = []
                let lo = Infinity, hi = -Infinity
                for (let i = start; i < Math.min(start + 24, d.hourly.time.length); ++i) {
                    const t = d.hourly.temperature_2m[i]
                    lo = Math.min(lo, t)
                    hi = Math.max(hi, t)
                    hh.push({
                        label: d.hourly.time[i].substring(11, 13),
                        temp: t,
                        pp: d.hourly.precipitation_probability?.[i] ?? 0
                    })
                }
                root.hours = hh
                root.tMin = lo
                root.tMax = hi

                // Daily.
                const out = []
                let dlo = Infinity, dhi = -Infinity
                for (let j = 0; j < d.daily.time.length; ++j) {
                    dlo = Math.min(dlo, d.daily.temperature_2m_min[j])
                    dhi = Math.max(dhi, d.daily.temperature_2m_max[j])
                    out.push({
                        date: d.daily.time[j],
                        code: d.daily.weather_code[j],
                        tmax: d.daily.temperature_2m_max[j],
                        tmin: d.daily.temperature_2m_min[j]
                    })
                }
                root.days = out
                root.dMin = dlo
                root.dMax = dhi

                root.loading = false

                // Kick off the reveal animations.
                root.chartProgress = 0
                root.barsProgress = 0
                chartAnim.restart()
                barsAnim.restart()
                chart.requestPaint()
            })
            .catch(e => {
                root.failed = true
                root.loading = false
                console.warn("weather popup: forecast falhou:", e.message)
            })
    }

    function weekday(dateStr, index) {
        if (index === 0)
            return "Hoje"
        return Qt.formatDate(new Date(`${dateStr}T00:00:00`), "ddd")
    }

    NumberAnimation { id: chartAnim; target: root; property: "chartProgress"; from: 0; to: 1; duration: 850; easing.type: Easing.OutCubic }
    NumberAnimation { id: barsAnim; target: root; property: "barsProgress"; from: 0; to: 1; duration: 600; easing.type: Easing.OutCubic }
    onChartProgressChanged: chart.requestPaint()

    // PopupShell assigns the payload (lat/lon/metrics/…) AFTER the content is created —
    // in its Loader.onLoaded, i.e. after this item's Component.onCompleted. So we can't
    // load there; instead wait for the coordinates to arrive and defer one tick with
    // Qt.callLater, so every payload key (incl. `metrics`) is set before we build the URL.
    // Load once the coordinates arrive, deferred via a 0ms Timer so every payload key
    // (incl. metrics) is set first. restart() coalesces the per-key change bursts.
    Timer {
        id: loadDefer
        interval: 0
        repeat: false
        onTriggered: root.loadAll()
    }
    function scheduleLoad() {
        if (!isNaN(root.lat) && !isNaN(root.lon))
            loadDefer.restart()
    }
    onLatChanged: scheduleLoad()
    onLonChanged: scheduleLoad()

    Component.onCompleted: {
        panelIn.start()
        scheduleLoad()
    }

    // Condition-driven animated backdrop (rain/clouds/sun/snow/storm), behind content.
    WeatherBackdrop {
        anchors.fill: parent
        code: root.code
        isDay: root.isDay
        accent: root.accent
        active: root.animatedBackground && !root.failed && root.code >= 0
        radius: 12
    }

    // Whole-panel entrance.
    Item {
        id: panel
        anchors.fill: parent
        opacity: 0
        y: 10

        ParallelAnimation {
            id: panelIn
            NumberAnimation { target: panel; property: "opacity"; from: 0; to: 1; duration: 200 }
            NumberAnimation { target: panel; property: "y"; from: 10; to: 0; duration: 240; easing.type: Easing.OutCubic }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            // ── Header: current conditions ────────────────────────────────
            Row {
                width: parent.width
                spacing: 12

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.wx.emoji
                    font.family: root.fontFamily
                    font.pointSize: root.fontSize + 20
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: root.current.temperature_2m !== undefined ? Math.round(root.current.temperature_2m) + "°C" : "—"
                        color: root.fg
                        font.family: root.fontFamily
                        font.pointSize: root.fontSize + 10
                        font.bold: true
                    }
                    Text {
                        text: root.wx.label
                        color: root.fg
                        opacity: 0.7
                        font.family: root.fontFamily
                        font.pointSize: root.fontSize
                    }
                }
                Item { width: 1; height: 1 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x
                    text: root.locationName
                    color: root.accent
                    font.family: root.fontFamily
                    font.pointSize: root.fontSize
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }

            // ── N metric chips (staggered fade-in) ────────────────────────
            Flow {
                width: parent.width
                spacing: 6

                Repeater {
                    model: root.failed ? [] : root.metrics

                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        required property int index
                        readonly property var meta: Wmo.metricLabel(modelData)
                        radius: 7
                        color: root.tint(root.fg, 0.07)
                        width: chipRow.implicitWidth + 16
                        height: 30
                        opacity: 0

                        Component.onCompleted: chipIn.start()
                        SequentialAnimation {
                            id: chipIn
                            PauseAnimation { duration: chip.index * 55 }
                            NumberAnimation { target: chip; property: "opacity"; to: 1; duration: 220 }
                        }

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: chip.meta.icon
                                font.family: root.fontFamily
                                font.pointSize: root.fontSize
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: chip.meta.label
                                    color: root.fg
                                    opacity: 0.6
                                    font.family: root.fontFamily
                                    font.pointSize: root.fontSize - 3
                                }
                                Text {
                                    text: root.fmt(chip.modelData)
                                    color: root.fg
                                    font.family: root.fontFamily
                                    font.pointSize: root.fontSize - 1
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            // ── 24h temperature chart (animated Canvas) ───────────────────
            Text {
                text: "Próximas 24h"
                color: root.fg
                opacity: 0.6
                font.family: root.fontFamily
                font.pointSize: root.fontSize - 1
            }
            Canvas {
                id: chart
                width: parent.width
                height: 120
                visible: !root.failed && root.hours.length > 0
                antialiasing: true
                renderTarget: Canvas.FramebufferObject

                // Geometry shared with the hover overlay below (keep in sync with onPaint).
                readonly property int padT: 14
                readonly property int padB: 18
                readonly property int padL: 6
                readonly property int padR: 26
                property int hoverIndex: -1
                property real hoverX: 0
                function xAtIndex(i) {
                    var n = root.hours.length
                    return n > 1 ? padL + (width - padL - padR) * i / (n - 1) : padL
                }
                function indexAtX(px) {
                    var n = root.hours.length
                    if (n < 2) return -1
                    var plotW = width - padL - padR
                    if (px < padL - 4 || px > padL + plotW + 4) return -1
                    return Math.max(0, Math.min(n - 1, Math.round((px - padL) / plotW * (n - 1))))
                }

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var W = width, H = height
                    var n = root.hours.length
                    if (n < 2)
                        return

                    var padT = chart.padT, padB = chart.padB, padL = chart.padL, padR = chart.padR
                    var plotW = W - padL - padR
                    var plotH = H - padT - padB
                    var range = (root.tMax - root.tMin) || 1

                    function xAt(i) { return padL + plotW * i / (n - 1) }
                    function yAt(t) { return padT + plotH - ((t - root.tMin) / range) * plotH }

                    // Precipitation-probability bars (faint), revealed with progress.
                    var shown = Math.max(2, Math.ceil(n * root.chartProgress))
                    var bw = plotW / n * 0.6
                    ctx.fillStyle = root.rgba(root.cool, 0.30)
                    for (var i = 0; i < shown; ++i) {
                        var pp = root.hours[i].pp / 100
                        if (pp <= 0)
                            continue
                        var bh = plotH * pp
                        ctx.fillRect(xAt(i) - bw / 2, padT + plotH - bh, bw, bh)
                    }

                    // Temperature area + line.
                    ctx.beginPath()
                    ctx.moveTo(xAt(0), yAt(root.hours[0].temp))
                    for (var j = 1; j < shown; ++j)
                        ctx.lineTo(xAt(j), yAt(root.hours[j].temp))
                    // Area fill down to baseline.
                    var lastX = xAt(shown - 1)
                    var grad = ctx.createLinearGradient(0, padT, 0, padT + plotH)
                    grad.addColorStop(0, root.rgba(root.warm, 0.45))
                    grad.addColorStop(1, root.rgba(root.warm, 0.02))
                    ctx.lineTo(lastX, padT + plotH)
                    ctx.lineTo(xAt(0), padT + plotH)
                    ctx.closePath()
                    ctx.fillStyle = grad
                    ctx.fill()

                    // Line on top.
                    ctx.beginPath()
                    ctx.moveTo(xAt(0), yAt(root.hours[0].temp))
                    for (var k = 1; k < shown; ++k)
                        ctx.lineTo(xAt(k), yAt(root.hours[k].temp))
                    ctx.strokeStyle = root.rgba(root.warm, 1)
                    ctx.lineWidth = 2
                    ctx.lineJoin = "round"
                    ctx.stroke()

                    // Min/max labels.
                    ctx.fillStyle = root.rgba(root.fg, 0.7)
                    ctx.font = (root.fontSize - 2) + "px " + root.canvasFontFamily()
                    ctx.textBaseline = "middle"
                    ctx.fillText(Math.round(root.tMax) + "°", W - padR + 4, yAt(root.tMax))
                    ctx.fillText(Math.round(root.tMin) + "°", W - padR + 4, yAt(root.tMin))

                    // Hour ticks every 6h.
                    ctx.fillStyle = root.rgba(root.fg, 0.45)
                    ctx.textBaseline = "alphabetic"
                    ctx.textAlign = "center"
                    for (var h = 0; h < n; h += 6) {
                        ctx.fillText(root.hours[h].label + "h", xAt(h), H - 4)
                    }
                }

                // Hover crosshair + per-hour tooltip (QML overlay, like the BTC chart).
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    onPositionChanged: function (mouse) {
                        chart.hoverX = mouse.x
                        chart.hoverIndex = chart.indexAtX(mouse.x)
                    }
                    onExited: chart.hoverIndex = -1
                }
                Rectangle {
                    visible: chart.hoverIndex >= 0
                    width: 1
                    color: root.tint(root.fg, 0.5)
                    x: chart.xAtIndex(chart.hoverIndex) - 0.5
                    y: chart.padT
                    height: chart.height - chart.padT - chart.padB
                }
                Rectangle {
                    id: hourTip
                    readonly property var hd: (chart.hoverIndex >= 0 && chart.hoverIndex < root.hours.length)
                        ? root.hours[chart.hoverIndex] : null
                    visible: hd !== null
                    z: 3
                    radius: 4
                    color: Qt.rgba(0, 0, 0, 0.82)
                    border.width: 1
                    border.color: root.tint(root.tipFg, 0.35)
                    width: hourCol.implicitWidth + 14
                    height: hourCol.implicitHeight + 10
                    x: {
                        var px = chart.hoverX + 12
                        if (px + width > chart.width) px = chart.hoverX - width - 12
                        return Math.max(0, Math.min(Math.max(0, chart.width - width), px))
                    }
                    y: 2
                    Column {
                        id: hourCol
                        x: 7; y: 5; spacing: 1
                        TipText {
                            text: hourTip.hd ? hourTip.hd.label + ":00" : ""
                            color: root.tint(root.tipFg, 0.65)
                        }
                        TipText {
                            text: hourTip.hd ? Math.round(hourTip.hd.temp) + "°" : ""
                            color: root.tipFg
                            font.bold: true
                        }
                        TipText {
                            text: hourTip.hd ? (hourTip.hd.pp > 0 ? hourTip.hd.pp + "% chuva" : "sem chuva") : ""
                            color: hourTip.hd && hourTip.hd.pp > 0 ? root.cool : root.tint(root.tipFg, 0.5)
                        }
                    }
                }
            }

            // ── 7-day forecast with animated range bars ───────────────────
            Column {
                width: parent.width
                spacing: 2

                Repeater {
                    model: root.failed ? [] : root.days

                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 26

                        Text {
                            id: dayLabel
                            anchors.verticalCenter: parent.verticalCenter
                            width: 52
                            text: root.weekday(modelData.date, index)
                            color: root.fg
                            opacity: index === 0 ? 1.0 : 0.85
                            font.family: root.fontFamily
                            font.pointSize: root.fontSize
                            font.bold: index === 0
                        }
                        Text {
                            id: dayIcon
                            anchors.left: dayLabel.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: Wmo.describe(modelData.code).emoji
                            font.family: root.fontFamily
                            font.pointSize: root.fontSize + 1
                        }
                        Text {
                            id: minLabel
                            anchors.left: dayIcon.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 26
                            text: Math.round(modelData.tmin) + "°"
                            color: root.fg
                            opacity: 0.55
                            horizontalAlignment: Text.AlignRight
                            font.family: root.fontFamily
                            font.pointSize: root.fontSize
                        }

                        // Range track + the day's min→max segment.
                        Rectangle {
                            id: track
                            anchors.left: minLabel.right
                            anchors.right: maxLabel.left
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            height: 5
                            radius: 2.5
                            color: root.tint(root.fg, 0.12)

                            readonly property real span: (root.dMax - root.dMin) || 1
                            Rectangle {
                                radius: 2.5
                                height: parent.height
                                x: parent.width * (modelData.tmin - root.dMin) / track.span
                                width: (parent.width * (modelData.tmax - modelData.tmin) / track.span) * root.barsProgress
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: root.cool }
                                    GradientStop { position: 1.0; color: root.warm }
                                }
                            }
                        }
                        Text {
                            id: maxLabel
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 26
                            text: Math.round(modelData.tmax) + "°"
                            color: root.fg
                            font.family: root.fontFamily
                            font.pointSize: root.fontSize
                            font.bold: true
                        }
                    }
                }
            }
        }
    }

    // Loading / error overlay.
    Text {
        anchors.centerIn: parent
        visible: root.loading || root.failed
        text: root.failed ? "Falha ao carregar a previsão" : "Carregando…"
        color: root.fg
        opacity: 0.7
        font.family: root.fontFamily
        font.pointSize: root.fontSize
    }
}
