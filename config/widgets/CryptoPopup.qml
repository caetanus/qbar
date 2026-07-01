import QtQuick
import QtQuick.Window
import QtCore
import QtWebSockets
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch
import "qrc:/qbar/Json.js" as QJson

// Candlestick / line popup for the Crypto widget — EXTERNAL (loaded from disk by Crypto.qml).
// The popup shell draws the themed #popup chrome; this is the content. Fetches Binance
// klines and draws candles or a line, with a price scale (Y) + time ticks (X), optional
// Bollinger Bands, volume and RSI panes, and an interval selector. `price`/`changePct`
// arrive from the payload.
Item {
    id: root
    implicitWidth: 624
    implicitHeight: 432
    width: implicitWidth
    height: implicitHeight
    focus: true

    Shortcut {
        sequences: ["Ctrl+Z"]
        onActivated: root.undoDrawing()
    }
    Keys.onPressed: function(event) {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
            root.undoDrawing()
            event.accepted = true
        }
    }

    // From the widget's payload.
    property var ticks: [
        { "label": "BTC", "symbol": "BTCUSDT", "base": "BTC", "quote": "USDT", "icon": "crypto-icons/btc.svg", "color": "#f7931a" },
        { "label": "ETH", "symbol": "ETHUSDT", "base": "ETH", "quote": "USDT", "icon": "crypto-icons/eth.svg", "color": "#627eea" },
        { "label": "XMR", "symbol": "XMRUSDT", "base": "XMR", "quote": "USDT", "icon": "crypto-icons/xmr.svg", "color": "#ff6600" }
    ]
    property int activeIndex: 0
    property real price: 0
    property real changePct: 0
    readonly property bool up: changePct >= 0
    readonly property var activeTick: normalizedTick(activeIndex)
    readonly property string activeLabel: activeTick.label
    readonly property string activeSymbol: activeTick.symbol
    readonly property string activeBase: activeTick.base
    readonly property string activeQuote: activeTick.quote
    // The window hosting this content (the popup overlay, or the detached toplevel). Window.window
    // must attach to an Item, so resolve it here on root rather than inside the Binding below.
    readonly property var hostWindow: Window.window

    // Theme-derived colours (candles are conventional green/red, independent of theme).
    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.55)
    readonly property color fgFaint: Qt.rgba(fg.r, fg.g, fg.b, 0.35)
    // The hover tooltip has a fixed dark background, so its text must stay light regardless
    // of theme (`fg` is dark on a light popup → black-on-black).
    readonly property color tipFg: "#eef2f7"
    readonly property color tipFgSoft: Qt.rgba(0.93, 0.95, 0.97, 0.6)
    readonly property color tipFgFaint: Qt.rgba(0.93, 0.95, 0.97, 0.35)
    readonly property color gridColor: Qt.rgba(fg.r, fg.g, fg.b, 0.11)
    // Crosshair scope labels: an inverse-contrast pill (box = the chart's text colour, ink =
    // the popup background) so the cursor's price/time reads clearly against the chart.
    readonly property color crossLabelInk: {
        if (cssTheme && cssTheme.loaded && theme.background !== undefined) {
            var c = cssTheme.parseColor(theme.background)
            if (c !== undefined && c !== null)
                return Qt.rgba(c.r, c.g, c.b, 1)
        }
        return "#15171c"
    }
    readonly property color accent: (cssTheme && cssTheme.loaded && theme.accent !== undefined)
        ? cssTheme.parseColor(theme.accent) : fg
    readonly property color upColor: "#33b864"
    readonly property color downColor: "#e8546a"
    readonly property color bbColor: Qt.rgba(0.46, 0.62, 0.96, 0.85)
    readonly property color bbFill: Qt.rgba(0.46, 0.62, 0.96, 0.08)

    // Controls / state. Persisted across opens/restarts by the Settings block below.
    property string period: "1d"
    property string chartType: "candle"  // "candle" | "line"
    property bool showBollinger: false
    property bool logScale: false        // logarithmic vs linear price axis
    property bool showVolume: true       // volume histogram pane under the price chart
    property bool showRsi: false         // Wilder RSI(14), overlaid on the volume pane
    property int barCount: 288            // data points selected by the active range
    // Time-window presets (a RANGE, not a candle size). Each window auto-picks the SMALLEST
    // Binance candle that fills it with a useful number of candles — the chip is the window,
    // the `api` is derived, so the two are never conflated. `bars` ≈ window / candle size.
    // Short windows use the 1s interval so e.g. "1m" is 60 candles, not 1.
    readonly property var periods: [
        { label: "1m",  api: "1s", bars: 60 },
        { label: "5m",  api: "1s", bars: 300 },
        { label: "10m", api: "1s", bars: 600 },
        { label: "30m", api: "1m", bars: 30 },
        { label: "1h",  api: "1m", bars: 60 },
        { label: "6h",  api: "5m", bars: 72 },
        { label: "12h", api: "5m", bars: 144 },
        { label: "1d",  api: "5m", bars: 288 },
        { label: "1w",  api: "1h", bars: 168 },
        { label: "1M",  api: "4h", bars: 180 },
        { label: "6M",  api: "1d", bars: 180 },
        { label: "1y",  api: "1d", bars: 365 },
        { label: "5y",  api: "1w", bars: 260 }
    ]
    readonly property int minBars: 5
    readonly property int maxBars: 1000
    readonly property int bbPeriod: 20
    readonly property real bbMult: 2.0
    readonly property int rsiPeriod: 14
    // Extra leading candles fetched only to warm up BB/RSI before the visible range.
    readonly property int warmup: Math.max(showBollinger ? bbPeriod - 1 : 0,
                                           showRsi ? rsiPeriod : 0)

    function isValidPeriod(label) {
        for (var i = 0; i < root.periods.length; ++i)
            if (root.periods[i].label === label)
                return true
        return false
    }

    function normalizedTick(index) {
        var list = root.ticks && root.ticks.length ? root.ticks : []
        var raw = list.length > 0 ? list[Math.max(0, Math.min(index, list.length - 1))] : ({})
        var label = String(raw.label || raw.base || raw.symbol || "ETH").toUpperCase()
        var symbol = String(raw.symbol || (label + "USDT")).toUpperCase()
        var quote = String(raw.quote || "USDT").toUpperCase()
        var base = String(raw.base || label).toUpperCase()
        return {
            label: label,
            symbol: symbol,
            base: base,
            quote: quote,
            icon: String(raw.icon || label.charAt(0)),
            color: String(raw.color || "#f7931a")
        }
    }

    function selectTick(index) {
        var count = root.ticks && root.ticks.length ? root.ticks.length : 1
        root.persistDrawings(root.period, root.drawings)
        liveReconnect.stop()
        liveSocket.active = false
        root.activeIndex = ((index % count) + count) % count
        root.price = 0
        root.changePct = 0
        root.pricePanPixels = 0
        root.manualPriceRange = false
        root.allCandles = []
        root.candles = []
        root.closes = []
        root.historyCandles = []
        root.historyCloses = []
        root.loadDrawings()
        root.scheduleReload()
    }

    function periodConfig() {
        for (var i = 0; i < root.periods.length; ++i) {
            if (root.periods[i].label === root.period)
                return root.periods[i]
        }
        return root.periods[0]
    }

    function selectPeriod(label) {
        for (var i = 0; i < root.periods.length; ++i) {
            if (root.periods[i].label === label) {
                root.barCount = root.periods[i].bars
                root.pricePanPixels = 0
                if (root.period === label && root.historyCandles.length > 0)
                    root.applyViewport(root.historyCandles.length)
                else {
                    root.persistDrawings(root.period, root.drawings)
                    root.period = label
                }
                return
            }
        }
    }

    // Data.
    property var allCandles: []  // full fetched set INCLUDING the warmup head (rebuilt on live ticks)
    property var candles: []   // [{ o, h, l, c, t }]
    property var closes: []
    property var bbUpper: []
    property var bbMid: []
    property var bbLower: []
    property var rsi: []
    property var historyCandles: []
    property var historyCloses: []
    property var historyBbUpper: []
    property var historyBbMid: []
    property var historyBbLower: []
    property var historyRsi: []
    property int viewEnd: 0
    property int viewStart: 0
    property int viewCount: 1
    property real minP: 0
    property real maxP: 0
    property bool loading: true
    property bool failed: false
    property bool backfilling: false     // a fetch of OLDER candles (drag-into-past) is in flight
    property bool noMoreHistory: false   // Binance returned nothing older → stop backfilling

    // Per-candle hover tooltip state.
    property int hoverIndex: -1
    property real hoverX: 0
    property real hoverY: 0
    property bool mouseInsideChart: false
    property bool draggingChart: false
    property real dragStartX: 0
    property real dragStartY: 0
    property int dragStartEnd: 0
    property real pricePanPixels: 0
    property real dragStartPricePan: 0
    property string interactionMode: "pan" // pan | free | line | fib | hline
    property var drawings: []
    property var undoStack: []
    property int movingHlineIndex: -1  // hline grabbed for dragging in pan+Shift edit mode
    property int shiftEditIndex: -1
    property real shiftEditStartX: 0
    property real shiftEditStartY: 0
    property bool shiftEditMoved: false
    property int editHoverIndex: -1    // drawing under the cursor while Shift-editing (highlight)
    // Box zoom: drag a rectangle (zoom mode) to frame a time+price region; "1:1" clears it.
    property bool manualPriceRange: false // an explicit Y window is set → recomputeScale defers
    property bool zoomBoxActive: false
    property real zoomBoxStartX: 0
    property real zoomBoxStartY: 0
    property real zoomBoxEndX: 0
    property real zoomBoxEndY: 0
    property var draftPoints: []
    property bool restoringDrawings: false
    readonly property bool drawingLayerNeeded: drawings.length > 0
        || draftPoints.length > 0
        || interactionMode !== "pan"
        || movingHlineIndex >= 0
        || shiftEditIndex >= 0
        || editHoverIndex >= 0
    readonly property int plotGutter: 52  // right gutter for price labels (must match onPaint)
    readonly property int plotFooter: 14  // bottom strip for time labels (must match onPaint)

    function requestDrawingPaint() {
        if (drawingLayerLoader.active && drawingLayerLoader.item)
            drawingLayerLoader.item.requestPaint()
    }

    function loadCandles() {
        root.failed = false
        root.noMoreHistory = false   // a fresh window may have more history available again
        root.backfilling = false
        root.manualPriceRange = false // a new window starts auto-fit (any box zoom is cleared)
        // Only show the "loading…" placeholder on the FIRST load. On a refetch (zoom /
        // interval change) keep the current chart painted until the new data arrives, so
        // the chart doesn't blink to "loading…" on every wheel step.
        if (root.candles.length === 0) {
            root.loading = true
            chart.requestPaint()
        }
        // Fetch enough EXTRA candles to warm up every indicator, then display only the
        // last `barCount`: indicators remain defined across the whole visible range.
        var cfg = root.periodConfig()
        var want = Math.min(1000, Math.max(120, root.barCount + root.warmup))
        Fetch.fetch("https://api.binance.com/api/v3/klines?symbol=" + encodeURIComponent(root.activeSymbol) + "&interval="
                + cfg.api + "&limit=" + want)
            .then(function (r) {
                if (!r.ok)
                    throw new Error("HTTP " + r.status)
                return r.json()
            })
            .then(function (arr) {
                root.loading = false
                var raw = []
                for (var i = 0; i < arr.length; ++i) {
                    raw.push({
                        t: arr[i][0],
                        o: parseFloat(arr[i][1]),
                        h: parseFloat(arr[i][2]),
                        l: parseFloat(arr[i][3]),
                        c: parseFloat(arr[i][4]),
                        v: parseFloat(arr[i][5])
                    })
                }
                // Keep the full fetched set (incl. the warmup head) so live websocket ticks
                // can recompute indicators correctly; rebuildFromAll slices out the
                // displayable history and warms up BB/RSI off the leading candles.
                root.allCandles = raw
                root.rebuildFromAll(true)
                root.connectLiveStream()
            })
            .catch(function (e) {
                root.loading = false
                root.failed = true
                console.warn("crypto popup: klines fetch failed for", root.activeSymbol, e)
                chart.requestPaint()
            })
    }

    function applyViewport(requestedEnd) {
        var total = root.historyCandles.length
        if (total === 0)
            return
        var count = Math.max(1, Math.min(root.barCount, total))
        // Permit up to 75% of the viewport beyond the newest candle for projections.
        // At least 25% of the window remains historical, preserving scale/context.
        var futureSpace = Math.floor(count * 0.75)
        var end = Math.max(count, Math.min(total + futureSpace, requestedEnd))
        var start = end - count
        var dataEnd = Math.min(total, end)
        root.viewStart = start
        root.viewEnd = end
        root.viewCount = count
        root.candles = root.historyCandles.slice(start, dataEnd)
        root.closes = root.historyCloses.slice(start, dataEnd)
        root.bbUpper = root.historyBbUpper.slice(start, dataEnd)
        root.bbMid = root.historyBbMid.slice(start, dataEnd)
        root.bbLower = root.historyBbLower.slice(start, dataEnd)
        root.rsi = root.historyRsi.slice(start, dataEnd)
        root.recomputeScale()
        chart.requestPaint()
        root.requestDrawingPaint()
        root.maybeBackfill() // auto-load older candles when panned near the start
    }

    // Recompute indicators from the full set (allCandles, incl. warmup) and slice out the
    // displayable history. Shared by the REST batch and every live websocket tick so the
    // BB/RSI warmup is always intact (recomputing off the already-sliced history would lose it).
    function rebuildFromAll(jumpToLatest) {
        var fcs = root.allCandles
        if (!fcs || fcs.length === 0)
            return
        var fcl = []
        for (var i = 0; i < fcs.length; ++i)
            fcl.push(fcs[i].c)
        var bb = root.showBollinger ? root.computeBollinger(fcl) : ({ upper: [], mid: [], lower: [] })
        var rsiValues = root.showRsi ? root.computeRsi(fcl) : []
        var usableStart = fcs.length > root.warmup ? root.warmup : 0
        root.historyCandles = fcs.slice(usableStart)
        root.historyCloses = fcl.slice(usableStart)
        root.historyBbUpper = bb.upper.slice(usableStart)
        root.historyBbMid = bb.mid.slice(usableStart)
        root.historyBbLower = bb.lower.slice(usableStart)
        root.historyRsi = rsiValues.slice(usableStart)
        if (jumpToLatest)
            root.applyViewport(root.historyCandles.length)
        else
            root.applyViewport(root.viewEnd)
    }

    // Merge one live kline into allCandles: update the still-forming candle, or append on
    // rollover. Follows the newest bar only when the viewport is already parked there, so a
    // tick never yanks the chart while the user is panned back in history.
    function applyLiveCandle(k) {
        var all = root.allCandles
        if (!all || all.length === 0)
            return
        var last = all[all.length - 1]
        if (k.t < last.t)
            return // stale tick for an already-closed candle
        var atLatest = root.viewEnd >= root.historyCandles.length
        var bar = { t: k.t, o: k.o, h: k.h, l: k.l, c: k.c, v: k.v }
        var copy = all.slice(0)
        if (k.t === last.t) {
            copy[copy.length - 1] = bar
            root.allCandles = copy
            root.rebuildFromAll(false)
        } else {
            copy.push(bar)
            if (copy.length > 1200)
                copy = copy.slice(copy.length - 1200)
            root.allCandles = copy
            root.rebuildFromAll(atLatest)
        }
    }

    // (Re)connect the live stream to the CURRENT candle size. Called after each batch fetch
    // so the websocket's interval always matches the candles on screen.
    function connectLiveStream() {
        liveSocket.active = false
        liveSocket.active = true
    }

    // Infinite history: when a pan/zoom brings the viewport near the oldest candle we have,
    // fetch the previous batch and prepend it. Called from applyViewport (every view change).
    function maybeBackfill() {
        if (root.backfilling || root.noMoreHistory || root.loading)
            return
        if (!root.allCandles || root.allCandles.length === 0 || root.allCandles.length >= 5000)
            return
        var threshold = Math.max(5, Math.floor(root.viewCount * 0.25))
        if (root.viewStart > threshold)
            return
        root.backfillOlder()
    }

    function backfillOlder() {
        root.backfilling = true
        var cfg = root.periodConfig()
        var oldest = root.allCandles[0].t
        var want = Math.min(1000, Math.max(300, root.barCount * 2))
        Fetch.fetch("https://api.binance.com/api/v3/klines?symbol=" + encodeURIComponent(root.activeSymbol) + "&interval="
                + cfg.api + "&endTime=" + (oldest - 1) + "&limit=" + want)
            .then(function (r) {
                if (!r.ok)
                    throw new Error("HTTP " + r.status)
                return r.json()
            })
            .then(function (arr) {
                if (!arr || arr.length === 0) {
                    root.noMoreHistory = true
                    root.backfilling = false
                    return
                }
                var older = []
                for (var i = 0; i < arr.length; ++i)
                    older.push({
                        t: arr[i][0],
                        o: parseFloat(arr[i][1]),
                        h: parseFloat(arr[i][2]),
                        l: parseFloat(arr[i][3]),
                        c: parseFloat(arr[i][4]),
                        v: parseFloat(arr[i][5])
                    })
                // endTime is inclusive — drop anything that overlaps what we already hold.
                while (older.length > 0 && older[older.length - 1].t >= oldest)
                    older.pop()
                if (older.length === 0) {
                    root.noMoreHistory = true
                    root.backfilling = false
                    return
                }
                var added = older.length
                root.allCandles = older.concat(root.allCandles)
                // Keep the view visually fixed: every existing candle shifted right by `added`.
                root.viewEnd += added
                root.dragStartEnd += added // if a drag is mid-flight, move its anchor too
                root.rebuildFromAll(false) // re-applies viewport at the offset viewEnd
                root.backfilling = false   // released last so the nested applyViewport can't re-enter
            })
            .catch(function (e) {
                root.backfilling = false
                console.warn("crypto backfill failed for", root.activeSymbol, e)
            })
    }

    function panChart(pixelDeltaX, pixelDeltaY) {
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var candleWidth = plotW / Math.max(1, root.viewCount)
        var candleDelta = Math.round(pixelDeltaX / candleWidth)
        // Horizontal drag scrolls time: re-fit the Y scale to the newly visible candles (via
        // applyViewport→recomputeScale) and drop any manual vertical nudge, so panning into
        // the past can't leave candles clipped against the old scale. A pure vertical drag
        // keeps nudging the price view (used to reach drawings/projections in empty space).
        if (candleDelta !== 0) {
            root.pricePanPixels = 0
            root.dragStartPricePan = 0
        } else {
            root.pricePanPixels = root.dragStartPricePan + pixelDeltaY
        }
        root.applyViewport(root.dragStartEnd - candleDelta)
    }

    function zoomChart(px, direction) {
        if (root.historyCandles.length === 0)
            return
        var oldCount = root.viewCount
        var step = Math.max(1, Math.round(oldCount * 0.12))
        var newCount = Math.max(root.minBars,
                                Math.min(root.maxBars, oldCount + direction * step))
        newCount = Math.min(newCount, root.historyCandles.length)
        if (newCount === oldCount)
            return
        root.pricePanPixels = 0 // zooming re-frames the Y scale; clear any vertical nudge
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var ratio = Math.max(0, Math.min(1, px / plotW))
        var oldStart = root.viewStart
        var anchor = oldStart + Math.floor(ratio * Math.max(0, oldCount - 1))
        var newStart = anchor - Math.floor(ratio * Math.max(0, newCount - 1))
        root.barCount = newCount
        root.applyViewport(newStart + newCount)
    }

    function pricePaneHeight() {
        var plotH = chart.height - root.plotFooter
        var indicatorH = (root.showVolume || root.showRsi) ? Math.round(plotH * 0.24) : 0
        return Math.max(1, plotH - indicatorH)
    }

    function drawingPoint(px, py) {
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var priceH = root.pricePaneHeight()
        var x = Math.max(0, Math.min(plotW, px))
        var y = Math.max(0, Math.min(priceH, py))
        var count = Math.max(1, root.viewCount)
        var start = root.viewStart
        var historyIndex = start + x / (plotW / count) - 0.5
        var normalized = (priceH - 2 + root.pricePanPixels - y) / Math.max(1, priceH - 4)
        var price
        if (root.logScale) {
            var logMin = root.log10(Math.max(1e-9, root.minP))
            var logRange = root.log10(Math.max(1e-9, root.maxP)) - logMin
            price = Math.pow(10, logMin + normalized * (logRange || 1))
        } else {
            price = root.minP + normalized * ((root.maxP - root.minP) || 1)
        }
        return { time: root.timeAtHistoryIndex(historyIndex), price: price }
    }

    function yForPrice(price) {
        var priceH = root.pricePaneHeight()
        var range = (root.maxP - root.minP) || 1
        var normalized
        if (root.logScale) {
            var logMin = root.log10(Math.max(1e-9, root.minP))
            var logRange = root.log10(Math.max(1e-9, root.maxP)) - logMin
            normalized = (root.log10(Math.max(1e-9, price)) - logMin) / (logRange || 1)
        } else {
            normalized = (price - root.minP) / range
        }
        return priceH - normalized * (priceH - 4) - 2 + root.pricePanPixels
    }

    function hlineSnapStep(price) {
        if (!isFinite(price) || price <= 0)
            return 1
        var magnitude = Math.pow(10, Math.floor(root.log10(price)))
        return Math.max(1, magnitude / 10)
    }

    function hlinePoint(px, py) {
        var point = root.drawingPoint(px, py)
        var step = root.hlineSnapStep(point.price)
        var snapped = Math.round(point.price / step) * step
        var snapY = root.yForPrice(snapped)
        if (Math.abs(snapY - py) <= 9)
            point.price = snapped
        return point
    }

    function beginDrawing(px, py) {
        root.draftPoints = [root.interactionMode === "hline"
            ? root.hlinePoint(px, py) : root.drawingPoint(px, py)]
    }

    function updateDrawing(px, py) {
        if (root.draftPoints.length === 0)
            return
        var point = root.interactionMode === "hline"
            ? root.hlinePoint(px, py) : root.drawingPoint(px, py)
        if (root.interactionMode === "free")
            root.draftPoints = root.draftPoints.concat([point])
        else if (root.interactionMode === "hline")
            root.draftPoints = [point] // a price level: only Y matters, drag adjusts it
        else
            root.draftPoints = [root.draftPoints[0], point]
    }

    function finishDrawing(px, py) {
        root.updateDrawing(px, py)
        // A horizontal S/R line needs just one point (the price); the others need two.
        var minPoints = root.interactionMode === "hline" ? 1 : 2
        if (root.draftPoints.length >= minPoints) {
            root.pushUndo()
            root.drawings = root.drawings.concat([{
                type: root.interactionMode,
                points: root.draftPoints
            }])
        }
        root.draftPoints = []
    }

    function selectDrawingTool(tool) {
        root.interactionMode = tool
        root.draftPoints = []
    }

    function clearDrawings() {
        root.pushUndo()
        root.drawings = []
        root.draftPoints = []
        root.interactionMode = "pan"
    }

    function cloneDrawings(value) {
        return JSON.parse(JSON.stringify(value || []))
    }

    function pushUndo() {
        var stack = root.undoStack.slice(0)
        stack.push(root.cloneDrawings(root.drawings))
        if (stack.length > 50)
            stack.shift()
        root.undoStack = stack
    }

    function undoDrawing() {
        if (root.zoomBoxActive) {
            root.zoomBoxActive = false
            return
        }
        if (root.draftPoints.length > 0) {
            root.draftPoints = []
            return
        }
        if (root.draggingChart) {
            root.draggingChart = false
            return
        }
        if (root.undoStack.length === 0)
            return
        var stack = root.undoStack.slice(0)
        var previous = stack.pop()
        root.undoStack = stack
        root.movingHlineIndex = -1
        root.shiftEditIndex = -1
        root.shiftEditMoved = false
        root.restoringDrawings = false
        root.drawings = root.cloneDrawings(previous)
    }

    // Distance from point (px,py) to the segment (x1,y1)-(x2,y2). (Math.hypot is avoided for
    // the same V4-portability caution as Math.log10.)
    function distToSegment(px, py, x1, y1, x2, y2) {
        var dx = x2 - x1, dy = y2 - y1
        var len2 = dx * dx + dy * dy
        if (len2 === 0)
            return Math.sqrt((px - x1) * (px - x1) + (py - y1) * (py - y1))
        var t = Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dy) / len2))
        var cx = x1 + t * dx, cy = y1 + t * dy
        return Math.sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy))
    }

    // Topmost drawing within a few px of (px,py), else -1. Mirrors drawingLayer's geometry.
    function drawingAt(px, py) {
        if (root.historyCandles.length === 0)
            return -1
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var priceH = root.pricePaneHeight()
        if (px < 0 || px > plotW || py < 0 || py > priceH)
            return -1
        var count = Math.max(1, root.viewCount)
        var start = root.viewStart
        var candleWidth = plotW / count
        var range = (root.maxP - root.minP) || 1
        var logMin = root.log10(Math.max(1e-9, root.minP))
        var logRange = root.log10(Math.max(1e-9, root.maxP)) - logMin
        function xOf(point) {
            var pi = point.time !== undefined
                ? (point.time - root.historyCandles[0].t) / root.candleDurationMs()
                : point.index
            return (pi - start + 0.5) * candleWidth
        }
        function yOf(point) {
            var nrm = root.logScale
                ? (root.log10(Math.max(1e-9, point.price)) - logMin) / (logRange || 1)
                : (point.price - root.minP) / range
            return priceH - nrm * (priceH - 4) - 2 + root.pricePanPixels
        }
        var tol = 6
        for (var i = root.drawings.length - 1; i >= 0; --i) {
            var d = root.drawings[i]
            if (d.type === "hline") {
                if (Math.abs(yOf(d.points[0]) - py) <= tol)
                    return i
            } else {
                var pts = d.points
                for (var s = 0; s < pts.length - 1; ++s)
                    if (root.distToSegment(px, py, xOf(pts[s]), yOf(pts[s]),
                                           xOf(pts[s + 1]), yOf(pts[s + 1])) <= tol)
                        return i
            }
        }
        return -1
    }

    function deleteDrawing(idx) {
        if (idx < 0 || idx >= root.drawings.length)
            return
        root.pushUndo()
        var copy = root.drawings.slice(0)
        copy.splice(idx, 1)
        root.drawings = copy // onDrawingsChanged repaints + persists
        root.editHoverIndex = -1
    }

    function moveHlineTo(idx, py) {
        if (idx < 0 || idx >= root.drawings.length)
            return
        var d = root.drawings[idx]
        if (d.type !== "hline")
            return
        var copy = root.drawings.slice(0)
        copy[idx] = { type: "hline", points: [{ time: d.points[0].time, price: root.hlinePoint(0, py).price }] }
        root.drawings = copy
    }

    // Zoom to the dragged rectangle: its x-span → candle range, its y-span → price window.
    function applyZoomBox() {
        if (root.historyCandles.length === 0)
            return
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var x1 = Math.min(root.zoomBoxStartX, root.zoomBoxEndX)
        var x2 = Math.max(root.zoomBoxStartX, root.zoomBoxEndX)
        var y1 = Math.min(root.zoomBoxStartY, root.zoomBoxEndY)
        var y2 = Math.max(root.zoomBoxStartY, root.zoomBoxEndY)
        if (x2 - x1 < 8 || y2 - y1 < 8) // ignore tiny accidental boxes
            return
        var candleWidth = plotW / Math.max(1, root.viewCount)
        var startIdx = Math.floor(root.viewStart + Math.max(0, x1) / candleWidth)
        var endIdx = Math.ceil(root.viewStart + Math.min(plotW, x2) / candleWidth)
        var newCount = Math.min(root.historyCandles.length, Math.max(root.minBars, endIdx - startIdx))
        var pa = root.drawingPoint(0, y1).price
        var pb = root.drawingPoint(0, y2).price
        var lo = Math.min(pa, pb), hi = Math.max(pa, pb)
        var pad = (hi - lo) * 0.02
        root.minP = lo - pad
        root.maxP = hi + pad
        root.manualPriceRange = true // hold this Y window until 1:1
        root.pricePanPixels = 0
        root.barCount = newCount
        root.applyViewport(startIdx + newCount)
    }

    // "1:1": turn the zoom off — back to the window's default count + auto-fit Y.
    function resetZoom() {
        root.manualPriceRange = false
        root.pricePanPixels = 0
        root.barCount = root.periodConfig().bars
        root.applyViewport(root.historyCandles.length)
    }

    function drawingsStorageKey(periodName) {
        return "crypto.drawings." + root.activeSymbol + "." + periodName
    }

    function persistDrawings(periodName, value) {
        var key = root.drawingsStorageKey(periodName)
        QJson.stringify(value)
            .then(function (encoded) { LocalStorage.setItem(key, encoded) })
            .catch(function (error) { console.warn("crypto drawings stringify failed:", error) })
    }

    function loadDrawings() {
        root.restoringDrawings = true
        root.drawings = []
        root.draftPoints = []
        root.undoStack = []
        root.restoringDrawings = false
        LocalStorage.getItem(root.drawingsStorageKey(root.period))
    }

    onDrawingsChanged: {
        root.requestDrawingPaint()
        if (!root.restoringDrawings)
            root.persistDrawings(root.period, root.drawings)
    }
    onDraftPointsChanged: root.requestDrawingPaint()

    // Wilder's RSI: seed with a simple average, then smooth gains/losses recursively.
    function computeRsi(closes) {
        var result = []
        var p = root.rsiPeriod
        for (var empty = 0; empty < Math.min(p, closes.length); ++empty)
            result.push(null)
        if (closes.length <= p)
            return result

        var gain = 0, loss = 0
        for (var i = 1; i <= p; ++i) {
            var delta = closes[i] - closes[i - 1]
            gain += Math.max(0, delta)
            loss += Math.max(0, -delta)
        }
        var avgGain = gain / p
        var avgLoss = loss / p
        function value(g, l) {
            if (g === 0 && l === 0) return 50
            if (l === 0) return 100
            return 100 - 100 / (1 + g / l)
        }
        result.push(value(avgGain, avgLoss))
        for (var j = p + 1; j < closes.length; ++j) {
            var change = closes[j] - closes[j - 1]
            avgGain = (avgGain * (p - 1) + Math.max(0, change)) / p
            avgLoss = (avgLoss * (p - 1) + Math.max(0, -change)) / p
            result.push(value(avgGain, avgLoss))
        }
        return result
    }

    // Pure: Bollinger Bands over a closes array → { upper, mid, lower } (null during warmup).
    function computeBollinger(closes) {
        var n = closes.length
        var p = root.bbPeriod
        var up = [], mid = [], lo = []
        for (var i = 0; i < n; ++i) {
            if (i < p - 1) {
                up.push(null); mid.push(null); lo.push(null)
                continue
            }
            var sum = 0
            for (var j = i - p + 1; j <= i; ++j)
                sum += closes[j]
            var m = sum / p
            var v = 0
            for (var k = i - p + 1; k <= i; ++k) {
                var d = closes[k] - m
                v += d * d
            }
            var sd = Math.sqrt(v / p)
            mid.push(m)
            up.push(m + root.bbMult * sd)
            lo.push(m - root.bbMult * sd)
        }
        return { upper: up, mid: mid, lower: lo }
    }

    function recomputeScale() {
        if (root.manualPriceRange)
            return // a box zoom set an explicit Y window; keep it until "1:1"
        var mn = Infinity, mx = -Infinity
        for (var i = 0; i < root.candles.length; ++i) {
            if (root.candles[i].l < mn) mn = root.candles[i].l
            if (root.candles[i].h > mx) mx = root.candles[i].h
        }
        if (root.showBollinger) {
            for (var j = 0; j < root.bbUpper.length; ++j) {
                if (root.bbUpper[j] !== null) {
                    if (root.bbLower[j] < mn) mn = root.bbLower[j]
                    if (root.bbUpper[j] > mx) mx = root.bbUpper[j]
                }
            }
        }
        // pad the range a touch so candles don't touch the edges
        var pad = (mx - mn) * 0.04
        root.minP = mn - pad
        root.maxP = mx + pad
    }

    function formatPrice(p) {
        return p >= 1000 ? (p / 1000).toFixed(p >= 10000 ? 1 : 2) + "k" : p.toFixed(0)
    }

    // Canvas-safe font family for ctx.font. Context2D validates against QFontDatabase, where the
    // "Sans Serif"/"Serif"/"Monospace" aliases are NOT real families (they warn). Map them to the
    // CSS generics it DOES accept (unquoted); a real family name is quoted (and may contain spaces).
    function canvasFontFamily() {
        var f = (typeof theme !== "undefined" && theme.fontFamily) ? theme.fontFamily : "sans-serif"
        var l = f.toLowerCase()
        if (l === "sans serif" || l === "sansserif" || l === "sans-serif") return "sans-serif"
        if (l === "serif") return "serif"
        if (l === "monospace" || l === "mono") return "monospace"
        if (l === "cursive" || l === "fantasy") return l
        return '"' + f + '"'
    }

    // QML's JavaScript runtime is not guaranteed to expose Math.log10. This is the
    // same base-10 logarithm, expressed with the universally supported natural log.
    function log10(value) {
        return Math.log(value) / 2.302585092994046
    }

    function formatAxisValue(p) {
        // A financial log chart transforms spacing, not the displayed price labels.
        return root.formatPrice(p)
    }

    function formatVolume(v) {
        if (v === undefined || v === null) return "—"
        if (v >= 1e6) return (v / 1e6).toFixed(2) + "M"
        if (v >= 1e3) return (v / 1e3).toFixed(2) + "k"
        return v.toFixed(2)
    }

    function formatTime(ms) {
        var d = new Date(ms)
        // Base the format on the candle SIZE, not the window label: daily/weekly candles →
        // date only; anything intraday → date + time of day.
        var api = root.periodConfig().api
        if (api === "1d" || api === "1w")
            return Qt.formatDateTime(d, "MMM d")
        if (api === "1s")
            return Qt.formatDateTime(d, "hh:mm:ss")
        return Qt.formatDateTime(d, "MMM d hh:mm")
    }

    function candleDurationMs() {
        var api = root.periodConfig().api
        if (api === "1s") return 1000
        if (api === "1m") return 60 * 1000
        if (api === "5m") return 5 * 60 * 1000
        if (api === "1h") return 60 * 60 * 1000
        if (api === "4h") return 4 * 60 * 60 * 1000
        if (api === "1d") return 24 * 60 * 60 * 1000
        if (api === "1w") return 7 * 24 * 60 * 60 * 1000
        return 60 * 1000
    }

    function timeAtHistoryIndex(index) {
        if (root.historyCandles.length === 0)
            return Date.now()
        return root.historyCandles[0].t + index * root.candleDurationMs()
    }

    // Price/time under the crosshair, for the scope axis labels. priceAtCursorY inverts the
    // current Y scale (incl. log + vertical pan via drawingPoint); timeAtCursorX maps the
    // pixel X to a (possibly fractional) history index and back to a timestamp.
    function priceAtCursorY(py) {
        return root.drawingPoint(0, py).price
    }
    function timeAtCursorX(px) {
        var plotW = Math.max(1, chart.width - root.plotGutter)
        var gi = root.viewStart + px / (plotW / Math.max(1, root.viewCount))
        return root.timeAtHistoryIndex(gi)
    }

    // Candle index under a plot-local x; -1 outside the plot area or with no data.
    // Mirrors onPaint's geometry: plotW = width - gutter, cw = plotW / n, xOf = i*cw + cw/2.
    function indexAt(px) {
        if (root.candles.length === 0)
            return -1
        var plotW = chart.width - root.plotGutter
        if (px < 0 || px > plotW)
            return -1
        var globalIndex = Math.floor(root.viewStart + px / (plotW / root.viewCount))
        var localIndex = globalIndex - root.viewStart
        return localIndex >= 0 && localIndex < root.candles.length ? localIndex : -1
    }

    Component.onCompleted: {
        // A persisted period from an older build (e.g. "1m"/"5y") may no longer exist.
        if (!root.isValidPeriod(root.period))
            root.period = root.periods[0].label
        root.selectPeriod(root.period)
        root.loadDrawings()
        root.scheduleReload()
    }
    Component.onDestruction: {
        reloadTimer.stop()
        liveReconnect.stop()
        liveSocket.active = false
        root.allCandles = []
        root.candles = []
        root.closes = []
        root.bbUpper = []
        root.bbMid = []
        root.bbLower = []
        root.rsi = []
        root.historyCandles = []
        root.historyCloses = []
        root.historyBbUpper = []
        root.historyBbMid = []
        root.historyBbLower = []
        root.historyRsi = []
        root.drawings = []
        root.undoStack = []
        root.draftPoints = []
    }
    onPeriodChanged: {
        // Settings may restore a stale period after onCompleted — self-heal to a valid one.
        if (!root.isValidPeriod(root.period)) {
            root.period = root.periods[0].label
            return
        }
        root.interactionMode = "pan"
        root.loadDrawings()
        root.scheduleReload()
    }
    onShowBollingerChanged: {
        if (root.showBollinger) {
            root.scheduleReload()
        } else {
            root.historyBbUpper = []
            root.historyBbMid = []
            root.historyBbLower = []
            root.bbUpper = []
            root.bbMid = []
            root.bbLower = []
            recomputeScale()
            chart.requestPaint()
            root.requestDrawingPaint()
        }
    }
    onChartTypeChanged: chart.requestPaint()
    onLogScaleChanged: { chart.requestPaint(); root.requestDrawingPaint() }
    onShowVolumeChanged: { chart.requestPaint(); root.requestDrawingPaint() }
    onShowRsiChanged: {
        if (root.showRsi) {
            root.scheduleReload()
        } else {
            root.historyRsi = []
            root.rsi = []
            chart.requestPaint()
            root.requestDrawingPaint()
        }
    }

    // Debounce reloads: Settings restoring several values at startup, and wheel-zoom bursts,
    // would otherwise each fire a klines fetch; collapse them into a single request.
    function scheduleReload() { reloadTimer.restart() }
    Timer { id: reloadTimer; interval: 140; repeat: false; onTriggered: root.loadCandles() }

    // Live tail. The REST fetch above batch-loads history up to now; this keeps the newest
    // candle(s) live without polling. Binance pushes a kline update (~1/s) for the current
    // candle size; applyLiveCandle merges each one. The url is bound to the active candle
    // size, so switching the window reconnects to the matching stream (connectLiveStream).
    // The socket lives only as long as this popup — closing it destroys the object → disconnect.
    WebSocket {
        id: liveSocket
        url: "wss://stream.binance.com:9443/ws/" + root.activeSymbol.toLowerCase() + "@kline_" + root.periodConfig().api
        active: false // connectLiveStream() turns it on once the first batch has loaded
        onTextMessageReceived: function (message) {
            try {
                var msg = JSON.parse(message)
                var k = msg && msg.k
                if (!k)
                    return
                root.applyLiveCandle({
                    t: k.t,
                    o: parseFloat(k.o), h: parseFloat(k.h),
                    l: parseFloat(k.l), c: parseFloat(k.c), v: parseFloat(k.v)
                })
                var last = parseFloat(k.c)
                if (!isNaN(last) && last > 0)
                    root.price = last // keep the header price live too
            } catch (e) {
                console.warn("crypto ws parse failed:", e)
            }
        }
        // Reconnect ladder: 5 → 10 → 15s, then 30s forever. Keep trying until the
        // stream is back — never give up — and reset the ladder on a clean connect.
        property int retry: 0
        readonly property var backoff: [5000, 10000, 15000, 30000]
        onStatusChanged: {
            if (liveSocket.status === WebSocket.Open) {
                liveSocket.retry = 0
                // connectLiveStream() toggles active off→on; the transient Closed
                // queues a reconnect — now that we're Open, cancel it.
                liveReconnect.stop()
            } else if (liveSocket.status === WebSocket.Error
                       || liveSocket.status === WebSocket.Closed) {
                if (liveSocket.status === WebSocket.Error)
                    console.warn("crypto ws error:", liveSocket.errorString)
                if (root.visible && !liveReconnect.running) {
                    liveReconnect.interval =
                        liveSocket.backoff[Math.min(liveSocket.retry, liveSocket.backoff.length - 1)]
                    liveReconnect.restart()
                }
            }
        }
    }
    Timer {
        id: liveReconnect
        repeat: false
        onTriggered: {
            if (!root.visible)
                return
            liveSocket.retry++   // escalate the ladder if this attempt fails too
            root.connectLiveStream()
        }
    }

    // When detached into its own toplevel, mirror the live header (pair + price + change) into the
    // window title, like a ticker tab. Guarded on `detachedWindow` so it never retitles the shared
    // popup-overlay window when this content is shown inline.
    Binding {
        target: root.hostWindow
        property: "title"
        when: (typeof detachedWindow !== "undefined") && detachedWindow && root.hostWindow !== null
        value: root.price > 0
            // Drop the icon from the window title when it's an image path (only glyphs read as text).
            ? ((/\.(svg|png|jpe?g)$/i.test(String(root.activeTick.icon)) ? "" : root.activeTick.icon + " ")
               + root.activeBase + "/" + root.activeQuote + "  $" + root.price.toFixed(2) + "  "
               + (root.up ? "▲ " : "▼ ") + root.changePct.toFixed(2) + "%")
            : "QBar Crypto Applet — " + root.activeBase + "/" + root.activeQuote
    }
    Connections {
        target: LocalStorage
        function onItemLoaded(requestId, key, value, found) {
            if (key !== root.drawingsStorageKey(root.period) || !found)
                return
            QJson.parse(String(value))
                .then(function (decoded) {
                    // The popup may have been destroyed while this async parse ran
                    // (close/detach) — a destroyed QObject id reads back as null.
                    if (root === null)
                        return
                    if (key !== root.drawingsStorageKey(root.period))
                        return
                    if (decoded === null || decoded === undefined
                            || decoded.length === undefined)
                        return
                    root.restoringDrawings = true
                    root.drawings = decoded
                    root.restoringDrawings = false
                })
                .catch(function (error) {
                    console.warn("crypto drawings parse failed:", error)
                })
        }
    }

    // Remember the user's chart options across opens and qbar restarts (QSettings-backed).
    Settings {
        category: "cryptoPopup"
        property alias period: root.period
        property alias chartType: root.chartType
        property alias showBollinger: root.showBollinger
        property alias logScale: root.logScale
        property alias showVolume: root.showVolume
        property alias showRsi: root.showRsi
    }

    // A small clickable label used for the interval / type / Bollinger toggles.
    component Chip: Item {
        id: chip
        property string label: ""
        property bool active: false
        signal clicked()
        implicitWidth: chipText.implicitWidth + 14
        implicitHeight: chipText.implicitHeight + 6
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: chip.active ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18) : "transparent"
            border.width: 1
            border.color: chip.active ? root.accent : root.fgFaint
        }
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.active ? root.accent : root.fgSoft
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize - 1
            font.bold: chip.active
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    // One line of the per-candle hover tooltip.
    component TipText: Text {
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize - 2
    }

    component CarouselArrow: Item {
        id: arrow
        property string symbol: ""
        signal clicked()
        implicitWidth: 20
        implicitHeight: 24
        opacity: enabled ? 1 : 0.3
        Text {
            anchors.centerIn: parent
            text: arrow.symbol
            color: root.fgSoft
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
        }
        MouseArea {
            anchors.fill: parent
            enabled: arrow.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: arrow.clicked()
        }
    }

    function scrollToolbar(delta) {
        toolbar.contentX = Math.max(0, Math.min(toolbar.contentWidth - toolbar.width,
                                                toolbar.contentX + delta))
    }

    Item {
        anchors.fill: parent
        anchors.margins: 14

        // Header: pair + live price + change.
        Row {
            id: headerRow
            anchors.left: parent.left
            anchors.top: parent.top
            spacing: 10
            Text {
                id: pairLabel
                anchors.verticalCenter: parent.verticalCenter
                text: root.activeTick.icon + "  " + root.activeBase + "/" + root.activeQuote
                color: root.fg
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 1
                font.bold: true

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectTick(root.activeIndex + 1)
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.price > 0
                text: "$" + root.price.toFixed(2)
                color: root.fg
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 1
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (root.up ? "▲ " : "▼ ") + root.changePct.toFixed(2) + "%"
                color: root.up ? root.upColor : root.downColor
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
            }
            Repeater {
                model: root.ticks ? root.ticks.length : 0
                delegate: Chip {
                    anchors.verticalCenter: parent.verticalCenter
                    label: root.normalizedTick(index).label
                    active: index === root.activeIndex
                    onClicked: root.selectTick(index)
                }
            }
        }

        // Drawing tools stay separate from the market controls at the bottom.
        Row {
            id: drawingToolbar
            anchors.left: parent.left
            anchors.top: headerRow.bottom
            anchors.topMargin: 6
            spacing: 6

            Chip {
                label: "Pan"
                active: root.interactionMode === "pan"
                onClicked: root.selectDrawingTool("pan")
            }
            Chip {
                label: "Free"
                active: root.interactionMode === "free"
                onClicked: root.selectDrawingTool("free")
            }
            Chip {
                label: "Line"
                active: root.interactionMode === "line"
                onClicked: root.selectDrawingTool("line")
            }
            Chip {
                label: "H-Line"
                active: root.interactionMode === "hline"
                onClicked: root.selectDrawingTool("hline")
            }
            Chip {
                label: "Fib"
                active: root.interactionMode === "fib"
                onClicked: root.selectDrawingTool("fib")
            }
            Chip {
                label: "Zoom"
                active: root.interactionMode === "zoom"
                onClicked: root.selectDrawingTool("zoom")
            }
            Chip {
                label: "1:1"
                active: root.manualPriceRange
                onClicked: root.resetZoom()
            }
            Chip {
                label: "Clear"
                onClicked: root.clearDrawings()
            }
        }

        Chip {
            anchors.right: parent.right
            anchors.top: headerRow.bottom
            anchors.topMargin: 6
            visible: typeof detachedWindow === "undefined" || !detachedWindow
            label: "Detach"
            onClicked: qbarPopups.detachPopup(popupId)
        }

        // One compact toolbar: left half is the period carousel, right half is fixed tools.
        Item {
            id: mainToolbar
            z: 20
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: parent.width
            height: 26

            Item {
                id: periodArea
                anchors.left: parent.left
                width: parent.width / 2 - 3
                height: parent.height

                CarouselArrow {
                    id: previousControl
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "‹"
                    enabled: toolbar.contentX > 0
                    onClicked: root.scrollToolbar(-Math.max(80, toolbar.width * 0.6))
                }

                Flickable {
                    id: toolbar
                    anchors.left: previousControl.right
                    anchors.right: nextControl.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    contentWidth: controlsRow.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    // Open the strip scrolled to the end: the longer windows (…1d, 1w,
                    // 1y) show by default, the sub-day fractionals are tucked off-screen
                    // to the LEFT — scroll left to reach them. callLater so the Row's
                    // width is final first.
                    Component.onCompleted: Qt.callLater(function () {
                        toolbar.contentX = Math.max(0, toolbar.contentWidth - toolbar.width)
                    })

                    Row {
                        id: controlsRow
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Repeater {
                            model: root.periods
                            Chip {
                                label: modelData.label
                                active: root.period === modelData.label
                                onClicked: root.selectPeriod(modelData.label)
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        onWheel: function (wheel) {
                            var delta = wheel.angleDelta.x !== 0
                                ? -wheel.angleDelta.x : -wheel.angleDelta.y
                            root.scrollToolbar(delta)
                            wheel.accepted = true
                        }
                    }
                }

                CarouselArrow {
                    id: nextControl
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "›"
                    enabled: toolbar.contentX < Math.max(0, toolbar.contentWidth - toolbar.width)
                    onClicked: root.scrollToolbar(Math.max(80, toolbar.width * 0.6))
                }
            }

            Row {
                spacing: 6
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width / 2 - 3

                Chip {
                    label: root.chartType === "candle" ? "Candle" : "Line"
                    active: true
                    onClicked: root.chartType = (root.chartType === "candle" ? "line" : "candle")
                }
                Chip {
                    label: "BB"
                    active: root.showBollinger
                    onClicked: root.showBollinger = !root.showBollinger
                }
                Chip {
                    label: "Log"
                    active: root.logScale
                    onClicked: root.logScale = !root.logScale
                }
                Chip {
                    label: "Vol"
                    active: root.showVolume
                    onClicked: root.showVolume = !root.showVolume
                }
                Chip {
                    label: "RSI 14"
                    active: root.showRsi
                    onClicked: root.showRsi = !root.showRsi
                }
            }
        }

        Canvas {
            id: chart
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: drawingToolbar.bottom
            anchors.topMargin: 8
            anchors.bottom: mainToolbar.top
            anchors.bottomMargin: 8
            antialiasing: true

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            // Scroll over the chart to zoom: up = fewer candles (in), down = more (out).
            // A MouseArea (not a WheelHandler) because the popup shell has a wheel-swallowing
            // MouseArea below the content (PopupShell.qml) — a WheelHandler here got bypassed,
            // but a content MouseArea sits above the shell's and receives the wheel first.
            // Mirrors PopupShell's proven wheel MouseArea (acceptedButtons: AllButtons); a
            // click on the chart is harmlessly absorbed (clicking inside the popup shouldn't
            // dismiss it anyway).
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                hoverEnabled: true
                onEntered: root.mouseInsideChart = true
                cursorShape: {
                    if (root.interactionMode === "zoom")
                        return Qt.CrossCursor
                    if (root.interactionMode === "pan") {
                        if (root.editHoverIndex >= 0)
                            return Qt.PointingHandCursor // hovering a drawing with Shift
                        return pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    }
                    return Qt.CrossCursor
                }
                onWheel: function (wheel) {
                    var dir = wheel.angleDelta.y > 0 ? -1 : 1
                    root.zoomChart(wheel.x, dir)
                    wheel.accepted = true
                }
                onPressed: function (mouse) {
                    if (root.interactionMode === "zoom") {
                        // Begin a marquee-zoom rectangle.
                        root.zoomBoxStartX = mouse.x; root.zoomBoxStartY = mouse.y
                        root.zoomBoxEndX = mouse.x; root.zoomBoxEndY = mouse.y
                        root.zoomBoxActive = true
                    } else if (root.interactionMode === "pan") {
                        // Shift = edit drawings: grab an hline to move it, or clear a normal line.
                        if (mouse.modifiers & Qt.ShiftModifier) {
                            var idx = root.drawingAt(mouse.x, mouse.y)
                            if (idx >= 0) {
                                root.shiftEditIndex = idx
                                root.shiftEditStartX = mouse.x
                                root.shiftEditStartY = mouse.y
                                root.shiftEditMoved = false
                                root.hoverIndex = -1
                                return
                            }
                        }
                        root.draggingChart = true
                        root.dragStartX = mouse.x
                        root.dragStartY = mouse.y
                        root.dragStartEnd = root.viewEnd
                        root.dragStartPricePan = root.pricePanPixels
                    } else {
                        root.beginDrawing(mouse.x, mouse.y)
                    }
                    root.hoverIndex = -1
                }
                onPositionChanged: function (mouse) {
                    root.hoverX = mouse.x
                    root.hoverY = mouse.y
                    if (root.zoomBoxActive) {
                        root.zoomBoxEndX = mouse.x; root.zoomBoxEndY = mouse.y
                    } else if (root.shiftEditIndex >= 0) {
                        var dx = mouse.x - root.shiftEditStartX
                        var dy = mouse.y - root.shiftEditStartY
                        if (!root.shiftEditMoved && dx * dx + dy * dy >= 16) {
                            root.shiftEditMoved = true
                            if (root.drawings[root.shiftEditIndex].type === "hline") {
                                root.pushUndo()
                                root.movingHlineIndex = root.shiftEditIndex
                                root.restoringDrawings = true // suppress per-move persistence
                            }
                        }
                        if (root.movingHlineIndex >= 0)
                            root.moveHlineTo(root.movingHlineIndex, mouse.y)
                    } else if (root.movingHlineIndex >= 0) {
                        root.moveHlineTo(root.movingHlineIndex, mouse.y)
                    } else if (root.draggingChart) {
                        root.panChart(mouse.x - root.dragStartX, mouse.y - root.dragStartY)
                    } else if (pressed && root.interactionMode !== "pan" && root.interactionMode !== "zoom") {
                        root.updateDrawing(mouse.x, mouse.y)
                    } else {
                        root.editHoverIndex = (root.interactionMode === "pan" && (mouse.modifiers & Qt.ShiftModifier))
                            ? root.drawingAt(mouse.x, mouse.y) : -1
                        root.hoverIndex = root.indexAt(mouse.x)
                    }
                }
                onReleased: function (mouse) {
                    if (root.zoomBoxActive) {
                        root.zoomBoxActive = false
                        root.applyZoomBox()
                    } else if (root.shiftEditIndex >= 0) {
                        if (root.movingHlineIndex >= 0) {
                            root.movingHlineIndex = -1
                            root.restoringDrawings = false
                            root.persistDrawings(root.period, root.drawings)
                        } else if (!root.shiftEditMoved) {
                            root.deleteDrawing(root.shiftEditIndex)
                        }
                        root.shiftEditIndex = -1
                        root.shiftEditMoved = false
                    } else if (root.movingHlineIndex >= 0) {
                        root.movingHlineIndex = -1
                        root.restoringDrawings = false
                        root.persistDrawings(root.period, root.drawings)
                    } else if (root.interactionMode === "pan") {
                        root.draggingChart = false
                    } else {
                        root.finishDrawing(mouse.x, mouse.y)
                    }
                    root.hoverX = mouse.x
                    root.hoverY = mouse.y
                    root.hoverIndex = root.indexAt(mouse.x)
                }
                onCanceled: {
                    root.draggingChart = false
                    root.zoomBoxActive = false
                    root.shiftEditIndex = -1
                    root.shiftEditMoved = false
                    if (root.movingHlineIndex >= 0) {
                        root.movingHlineIndex = -1
                        root.restoringDrawings = false
                    }
                }
                onExited: {
                    root.mouseInsideChart = false
                    root.hoverIndex = -1
                }
            }

            Loader {
                id: drawingLayerLoader
                anchors.fill: parent
                z: 1
                active: root.drawingLayerNeeded
                onLoaded: item.requestPaint()

                sourceComponent: Canvas {
                    antialiasing: true
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if ((root.drawings.length === 0 && root.draftPoints.length === 0)
                                || root.historyCandles.length === 0)
                            return

                        var plotW = width - root.plotGutter
                        var priceH = root.pricePaneHeight()
                        var count = Math.max(1, root.viewCount)
                        var start = root.viewStart
                        var candleWidth = plotW / count
                        var range = (root.maxP - root.minP) || 1
                        var logMin = root.log10(Math.max(1e-9, root.minP))
                        var logRange = root.log10(Math.max(1e-9, root.maxP)) - logMin
                        function xOf(point) {
                        // Persist time rather than an array index so drawings survive
                        // newly arrived candles and still align after a restart.
                        var pointIndex = point.time !== undefined
                            ? (point.time - root.historyCandles[0].t) / root.candleDurationMs()
                            : point.index // compatibility with drawings saved by older builds
                        return (pointIndex - start + 0.5) * candleWidth
                    }
                    function yOf(point) {
                        var normalized = root.logScale
                            ? (root.log10(Math.max(1e-9, point.price)) - logMin) / (logRange || 1)
                            : (point.price - root.minP) / range
                        return priceH - normalized * (priceH - 4) - 2 + root.pricePanPixels
                    }
                    function strokePath(points) {
                        if (points.length < 2) return
                        ctx.beginPath()
                        ctx.moveTo(xOf(points[0]), yOf(points[0]))
                        for (var p = 1; p < points.length; ++p)
                            ctx.lineTo(xOf(points[p]), yOf(points[p]))
                        ctx.stroke()
                    }
                    function drawFib(points) {
                        if (points.length < 2) return
                        var a = points[0], b = points[points.length - 1]
                        var left = Math.min(xOf(a), xOf(b))
                        var right = plotW
                        var levels = [0, 0.236, 0.382, 0.5, 0.618, 0.786, 1]
                        ctx.font = (theme.fontSize - 3) + "px " + root.canvasFontFamily()
                        for (var f = 0; f < levels.length; ++f) {
                            var level = levels[f]
                            var point = { price: a.price + (b.price - a.price) * level }
                            var y = yOf(point)
                            ctx.globalAlpha = level === 0 || level === 1 ? 0.9 : 0.6
                            ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                            ctx.fillText(Math.round(level * 1000) / 10 + "%", left + 4, y - 3)
                        }
                        ctx.globalAlpha = 1
                    }
                    function drawHLine(points) {
                        // Support/resistance: a full-width line pinned to a price level (only
                        // the Y/price matters), with the level labelled at the left edge.
                        if (points.length < 1) return
                        var y = yOf(points[0])
                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(plotW, y); ctx.stroke()
                        ctx.font = (theme.fontSize - 3) + "px " + root.canvasFontFamily()
                        ctx.fillText(root.formatPrice(points[0].price), 4, y - 3)
                    }

                    ctx.save()
                    ctx.beginPath(); ctx.rect(0, 0, plotW, priceH); ctx.clip()
                    ctx.strokeStyle = root.accent
                    ctx.fillStyle = root.accent
                    ctx.lineWidth = 1.5
                    ctx.lineJoin = "round"
                    ctx.lineCap = "round"

                    var items = root.drawings.slice(0)
                    if (root.draftPoints.length > 0)
                        items.push({ type: root.interactionMode, points: root.draftPoints })
                    for (var i = 0; i < items.length; ++i) {
                        var hl = (i === root.editHoverIndex || i === root.movingHlineIndex)
                        ctx.lineWidth = hl ? 2.6 : 1.5
                        ctx.globalAlpha = hl ? 1.0 : 0.92
                        if (items[i].type === "fib")
                            drawFib(items[i].points)
                        else if (items[i].type === "hline")
                            drawHLine(items[i].points)
                        else
                            strokePath(items[i].points)
                    }
                        ctx.globalAlpha = 1
                        ctx.restore()
                    }
                }
            }

            // Per-candle hover crosshair + OHLC tooltip — a QML overlay (Rectangle/Text), NOT
            // a canvas repaint, so hovering never re-tessellates the chart or stalls the loop.
            Rectangle {
                visible: root.mouseInsideChart
                width: 1
                height: chart.height - root.plotFooter
                color: root.fgSoft
                y: 0
                x: Math.max(0, Math.min(chart.width - root.plotGutter, root.hoverX)) - width / 2
            }

            Rectangle {
                visible: root.mouseInsideChart
                x: 0
                y: Math.max(0, Math.min(chart.height - root.plotFooter - height, root.hoverY))
                width: chart.width - root.plotGutter
                height: 1
                color: root.fgSoft
            }

            // Scope readouts: the cursor's price on the right (Y) scale and time on the bottom
            // (X) scale, each a rounded inverse-contrast pill aligned to the crosshair. Hidden
            // during an active drag/zoom/hline-move so they don't fight those gestures.
            readonly property bool scopeVisible: root.mouseInsideChart && !root.draggingChart
                && !root.zoomBoxActive && root.movingHlineIndex < 0
            Rectangle {
                visible: chart.scopeVisible
                z: 4
                radius: 3
                color: root.fg
                height: priceScopeText.implicitHeight + 4
                width: priceScopeText.implicitWidth + 10
                x: chart.width - root.plotGutter + 1
                y: Math.max(0, Math.min(chart.height - root.plotFooter - height, root.hoverY - height / 2))
                Text {
                    id: priceScopeText
                    anchors.centerIn: parent
                    text: root.formatAxisValue(root.priceAtCursorY(root.hoverY))
                    color: root.crossLabelInk
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 2
                }
            }
            Rectangle {
                visible: chart.scopeVisible
                z: 4
                radius: 3
                color: root.fg
                height: timeScopeText.implicitHeight + 4
                width: timeScopeText.implicitWidth + 12
                y: chart.height - height
                x: Math.max(0, Math.min(chart.width - root.plotGutter - width, root.hoverX - width / 2))
                Text {
                    id: timeScopeText
                    anchors.centerIn: parent
                    text: root.formatTime(root.timeAtCursorX(root.hoverX))
                    color: root.crossLabelInk
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 2
                }
            }

            // Marquee-zoom selection rectangle (drawn live while dragging in "zoom" mode).
            Rectangle {
                visible: root.zoomBoxActive
                z: 5
                x: Math.min(root.zoomBoxStartX, root.zoomBoxEndX)
                y: Math.min(root.zoomBoxStartY, root.zoomBoxEndY)
                width: Math.abs(root.zoomBoxEndX - root.zoomBoxStartX)
                height: Math.abs(root.zoomBoxEndY - root.zoomBoxStartY)
                color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18)
                border.width: 2
                border.color: root.accent
                antialiasing: true
            }

            Rectangle {
                id: candleTip
                readonly property var cd: (root.hoverIndex >= 0 && root.hoverIndex < root.candles.length)
                    ? root.candles[root.hoverIndex] : null
                visible: cd !== null && !root.draggingChart
                z: 2
                radius: 4
                color: Qt.rgba(0, 0, 0, 0.82)
                border.width: 1
                border.color: root.tipFgFaint
                width: tipCol.implicitWidth + 16
                height: tipCol.implicitHeight + 12
                // follow the cursor; flip left near the right edge; clamp inside the plot
                x: {
                    var px = root.hoverX + 14
                    if (px + width > chart.width - root.plotGutter)
                        px = root.hoverX - width - 14
                    return Math.max(0, Math.min(Math.max(0, chart.width - width), px))
                }
                y: Math.max(0, Math.min(chart.height - root.plotFooter - height, root.hoverY - height / 2))

                Column {
                    id: tipCol
                    x: 8
                    y: 6
                    spacing: 1
                    TipText {
                        text: candleTip.cd ? root.formatTime(candleTip.cd.t) : ""
                        color: root.tipFgSoft
                    }
                    TipText {
                        text: candleTip.cd ? "O  " + candleTip.cd.o.toFixed(2) : ""
                        color: root.tipFg
                    }
                    TipText {
                        text: candleTip.cd ? "H  " + candleTip.cd.h.toFixed(2) : ""
                        color: root.upColor
                    }
                    TipText {
                        text: candleTip.cd ? "L  " + candleTip.cd.l.toFixed(2) : ""
                        color: root.downColor
                    }
                    TipText {
                        text: candleTip.cd ? "C  " + candleTip.cd.c.toFixed(2) : ""
                        color: candleTip.cd && candleTip.cd.c >= candleTip.cd.o
                            ? root.upColor : root.downColor
                    }
                    TipText {
                        text: candleTip.cd ? "V  " + root.formatVolume(candleTip.cd.v) : ""
                        color: root.tipFgSoft
                    }
                    TipText {
                        readonly property var value: root.hoverIndex >= 0
                            ? root.rsi[root.hoverIndex] : null
                        visible: root.showRsi && value !== null && value !== undefined
                        text: visible ? "RSI  " + value.toFixed(1) : ""
                        color: value >= 70 ? root.downColor
                            : (value <= 30 ? root.upColor : root.tipFgSoft)
                    }
                }
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.font = (theme.fontSize - 2) + "px " + root.canvasFontFamily()

                if (root.failed) {
                    ctx.fillStyle = root.fgSoft
                    ctx.fillText("could not load candles", 4, 18)
                    return
                }
                var cs = root.candles
                if (root.loading || !cs || cs.length === 0) {
                    ctx.fillStyle = root.fgSoft
                    ctx.fillText("loading…", 4, 18)
                    return
                }

                var gutter = root.plotGutter // right gutter for price labels
                var footer = root.plotFooter // bottom strip for time labels
                var plotW = width - gutter
                var plotH = height - footer
                // Volume and RSI share one lower pane, each with its own normalized scale.
                var indicatorH = (root.showVolume || root.showRsi) ? Math.round(plotH * 0.24) : 0
                var priceH = plotH - indicatorH
                var rsiTop = priceH
                var range = (root.maxP - root.minP) || 1
                var n = cs.length
                var cw = plotW / Math.max(1, root.viewCount)
                // Log-scale bounds (prices are always > 0); falls back to linear.
                var lmin = root.log10(Math.max(1e-9, root.minP))
                var lmax = root.log10(Math.max(1e-9, root.maxP))
                var lr = (lmax - lmin) || 1
                function yOf(p) {
                    if (root.logScale)
                        return priceH - ((root.log10(Math.max(1e-9, p)) - lmin) / lr)
                            * (priceH - 4) - 2 + root.pricePanPixels
                    return priceH - ((p - root.minP) / range) * (priceH - 4) - 2
                        + root.pricePanPixels
                }
                function xOf(i) { return i * cw + cw / 2 }

                // ── price scale (Y): gridlines + right-edge labels ──
                var ticks = 4
                ctx.lineWidth = 1
                for (var t = 0; t <= ticks; ++t) {
                    // Keep ticks on-screen and invert the current pan transform to find
                    // the price at each fixed Y position. This extends the scale into
                    // empty space instead of moving the labels out with the candles.
                    var gy = priceH - 2 - (priceH - 4) * t / ticks
                    var normalized = (priceH - 2 + root.pricePanPixels - gy)
                        / Math.max(1, priceH - 4)
                    var pv = root.logScale
                        ? Math.pow(10, lmin + lr * normalized)
                        : (root.minP + range * normalized)
                    ctx.strokeStyle = root.gridColor
                    ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(plotW, gy); ctx.stroke()
                    ctx.fillStyle = root.fgSoft
                    ctx.fillText(root.formatAxisValue(pv), plotW + 6, gy + 4)
                }

                // ── time scale (X): a few ticks along the bottom ──
                var xticks = 3
                ctx.fillStyle = root.fgFaint
                for (var xt = 0; xt <= xticks; ++xt) {
                    var ratioX = xt / xticks
                    var historyIndex = root.viewStart
                        + Math.round(Math.max(0, root.viewCount - 1) * ratioX)
                    var lx = cw / 2 + (plotW - cw) * ratioX
                    var lbl = root.formatTime(root.timeAtHistoryIndex(historyIndex))
                    var tw = ctx.measureText(lbl).width
                    var tx = Math.max(0, Math.min(plotW - tw, lx - tw / 2))
                    ctx.fillText(lbl, tx, height - 3)
                }

                // Price marks stay clipped to their pane while vertical panning.
                ctx.save()
                ctx.beginPath(); ctx.rect(0, 0, plotW, priceH); ctx.clip()

                // ── Bollinger band fill (between upper/lower) ──
                if (root.showBollinger) {
                    var first = 0 // displayed BB arrays are warmed up (no leading nulls)
                    if (n > first) {
                        ctx.fillStyle = root.bbFill
                        ctx.beginPath()
                        var started = false
                        for (var bu = first; bu < n; ++bu) {
                            if (root.bbUpper[bu] === null) continue
                            if (!started) { ctx.moveTo(xOf(bu), yOf(root.bbUpper[bu])); started = true }
                            else ctx.lineTo(xOf(bu), yOf(root.bbUpper[bu]))
                        }
                        for (var bl = n - 1; bl >= first; --bl) {
                            if (root.bbLower[bl] === null) continue
                            ctx.lineTo(xOf(bl), yOf(root.bbLower[bl]))
                        }
                        ctx.closePath(); ctx.fill()
                    }
                }

                // ── price series: candles or line ──
                if (root.chartType === "candle") {
                    var bw = Math.max(1.0, cw * 0.62)
                    for (var i = 0; i < n; ++i) {
                        var c = cs[i]
                        var x = xOf(i)
                        var green = c.c >= c.o
                        ctx.strokeStyle = green ? root.upColor : root.downColor
                        ctx.fillStyle = ctx.strokeStyle
                        ctx.lineWidth = 1
                        ctx.beginPath(); ctx.moveTo(x, yOf(c.h)); ctx.lineTo(x, yOf(c.l)); ctx.stroke()
                        var top = yOf(Math.max(c.o, c.c))
                        var bot = yOf(Math.min(c.o, c.c))
                        ctx.fillRect(x - bw / 2, top, bw, Math.max(1.0, bot - top))
                    }
                } else {
                    ctx.strokeStyle = root.accent
                    ctx.lineWidth = 1.6
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    for (var li = 0; li < n; ++li) {
                        var ly = yOf(cs[li].c)
                        if (li === 0) ctx.moveTo(xOf(li), ly)
                        else ctx.lineTo(xOf(li), ly)
                    }
                    ctx.stroke()
                }
                ctx.restore()

                // ── volume histogram (bottom pane) ──
                if (indicatorH > 6) {
                    // One separator for the combined volume + RSI pane.
                    ctx.strokeStyle = root.gridColor
                    ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(0, priceH); ctx.lineTo(plotW, priceH); ctx.stroke()
                }
                if (root.showVolume && indicatorH > 6) {
                    var maxV = 0
                    for (var mv = 0; mv < n; ++mv) {
                        var vv0 = cs[mv].v || 0
                        if (vv0 > maxV) maxV = vv0
                    }
                    maxV = maxV || 1
                    var vbw = Math.max(1.0, cw * 0.62)
                    for (var vi = 0; vi < n; ++vi) {
                        var vc = cs[vi]
                        var vh = ((vc.v || 0) / maxV) * (indicatorH - 4)
                        var vcol = (vc.c >= vc.o) ? root.upColor : root.downColor
                        ctx.fillStyle = Qt.rgba(vcol.r, vcol.g, vcol.b, 0.45)
                        ctx.fillRect(xOf(vi) - vbw / 2, priceH + indicatorH - vh,
                                     vbw, Math.max(1.0, vh))
                    }
                }

                // ── RSI(14), overlaid on the volume pane with a 0–100 scale ──
                if (root.showRsi && indicatorH > 10) {
                    function rsiY(value) {
                        return rsiTop + (100 - value) / 100 * indicatorH
                    }
                    var levels = [70, 30]
                    for (var ri = 0; ri < levels.length; ++ri) {
                        var level = levels[ri]
                        var levelY = rsiY(level)
                        ctx.setLineDash([3, 3])
                        ctx.beginPath(); ctx.moveTo(0, levelY); ctx.lineTo(plotW, levelY); ctx.stroke()
                        ctx.setLineDash([])
                        ctx.fillStyle = root.fgFaint
                        ctx.fillText(String(level), plotW + 6, levelY + 4)
                    }
                    ctx.strokeStyle = root.accent
                    ctx.lineWidth = 1.4
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    var rsiStarted = false
                    for (var rv = 0; rv < n; ++rv) {
                        var rsiValue = root.rsi[rv]
                        if (rsiValue === null || rsiValue === undefined) continue
                        if (!rsiStarted) {
                            ctx.moveTo(xOf(rv), rsiY(rsiValue))
                            rsiStarted = true
                        } else {
                            ctx.lineTo(xOf(rv), rsiY(rsiValue))
                        }
                    }
                    if (rsiStarted) ctx.stroke()
                }

                // ── Bollinger lines (upper / mid / lower) ──
                if (root.showBollinger) {
                    ctx.save()
                    ctx.beginPath(); ctx.rect(0, 0, plotW, priceH); ctx.clip()
                    function bbLine(series, color, dashMid) {
                        ctx.strokeStyle = color
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        var on = false
                        for (var i = 0; i < n; ++i) {
                            if (series[i] === null) continue
                            if (!on) { ctx.moveTo(xOf(i), yOf(series[i])); on = true }
                            else ctx.lineTo(xOf(i), yOf(series[i]))
                        }
                        ctx.stroke()
                    }
                    bbLine(root.bbUpper, root.bbColor)
                    bbLine(root.bbLower, root.bbColor)
                    bbLine(root.bbMid, root.fgSoft)
                    ctx.restore()
                }
            }
        }
    }
}
