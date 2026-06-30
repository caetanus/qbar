import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch

// Config:
// "custom/crypto": {
//   "source": "widgets/Crypto.qml",
//   "ticks": [
//     { "label": "BTC", "symbol": "BTCUSDT", "base": "BTC", "quote": "USDT", "icon": "₿", "color": "#f7931a" },
//     { "label": "ETH", "symbol": "ETHUSDT", "base": "ETH", "quote": "USDT", "icon": "Ξ", "color": "#627eea" },
//     { "label": "XMR", "symbol": "XMRUSDT", "base": "XMR", "quote": "USDT", "icon": "ɱ", "color": "#ff6600" }
//   ]
// }
QBar.CssRect {
    id: root

    property string toolId: ""
    readonly property var widgetConfig: (typeof customTools !== "undefined" && customTools && customTools[toolId])
        ? customTools[toolId] : ({})
    readonly property var configuredTicks: widgetConfig.ticks && widgetConfig.ticks.length
        ? widgetConfig.ticks
        : [
            { "label": "BTC", "symbol": "BTCUSDT", "base": "BTC", "quote": "USDT", "icon": "₿", "color": "#f7931a" },
            { "label": "ETH", "symbol": "ETHUSDT", "base": "ETH", "quote": "USDT", "icon": "Ξ", "color": "#627eea" },
            { "label": "XMR", "symbol": "XMRUSDT", "base": "XMR", "quote": "USDT", "icon": "ɱ", "color": "#ff6600" }
        ]
    property int activeIndex: 0
    readonly property string resolvedCssId: toolId === "custom/btc" ? "custom-btc" : "custom-crypto"
    readonly property string popupName: toolId === "custom/btc" ? "btc" : "crypto"

    cssId: resolvedCssId
    height: theme.height
    property int preferredWidth: Math.max(1, content.implicitWidth + 14)
    width: Math.max(1, preferredWidth)

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

    property real price: 0
    property real changePct: 0
    readonly property bool up: changePct >= 0
    readonly property color tint: up ? "#33b864" : "#e8546a"
    readonly property var tick: normalizedTick(activeIndex)

    property bool loaded: false
    property bool stale: false
    property bool _inFlight: false
    property bool _failed: false
    readonly property int _pollMs: 30000
    readonly property int _retryMs: 5000

    function normalizedTick(index) {
        var list = configuredTicks && configuredTicks.length ? configuredTicks : []
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

    function setActiveIndex(index) {
        var count = configuredTicks && configuredTicks.length ? configuredTicks.length : 1
        activeIndex = ((index % count) + count) % count
        loaded = false
        stale = false
        _failed = false
        price = 0
        changePct = 0
        pollTimer.stop()
        refresh()
    }

    function refresh() {
        if (root._inFlight)
            return
        root._inFlight = true
        var symbol = root.tick.symbol
        Fetch.fetch("https://api.binance.com/api/v3/ticker/24hr?symbol=" + encodeURIComponent(symbol), { timeout: 10000 })
            .then((r) => {
                if (!r.ok)
                    throw new Error("HTTP " + r.status)
                return r.json()
            })
            .then((d) => {
                root.price = parseFloat(d.lastPrice)
                root.changePct = parseFloat(d.priceChangePercent)
                root.loaded = true
                root.stale = false
                root._failed = false
            })
            .catch((e) => {
                console.warn("crypto widget: ticker fetch failed for", symbol, e)
                root._failed = true
                if (root.loaded)
                    root.stale = true
            })
            .then(() => {
                root._inFlight = false
                pollTimer.interval = root._failed ? root._retryMs : root._pollMs
                pollTimer.restart()
            })
    }

    function compactPrice(value) {
        if (!isFinite(value) || value <= 0)
            return "…"
        if (value >= 1000)
            return (value / 1000).toFixed(value >= 10000 ? 1 : 2) + "k"
        if (value >= 1)
            return value.toFixed(value >= 100 ? 0 : 2)
        return value.toPrecision(3)
    }

    Component.onCompleted: {
        preferredWidthUpdated(preferredWidth)
        refresh()
    }

    Timer {
        id: pollTimer
        interval: root._pollMs
        repeat: false
        onTriggered: root.refresh()
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 5

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.tick.icon
            color: root.tick.color
            // Crypto icons are Unicode symbols (₿ Ξ ɱ …). The configured bar font often
            // carries ₿ but not the rarer Greek/IPA letters, so a single font.family
            // renders BTC and tofus the rest. List broad-coverage fallbacks: Qt fills
            // any glyph the primary family is missing from the next family that has it.
            font.families: [theme.fontFamily, "Noto Sans", "DejaVu Sans"]
            font.pointSize: theme.fontSize
            font.bold: true
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.up ? "▲" : "▼"
            color: root.tint
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
        }
        QBar.CssText {
            cssId: root.resolvedCssId
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.stale ? 0.5 : 1.0
            text: root.loaded
                ? root.tick.label + " " + root.compactPrice(root.price) + " (" + root.changePct.toFixed(1) + "%)"
                : root.tick.label + " …"
        }
    }

    QBar.Popup {
        id: popup
        name: root.popupName
        anchorItem: root
        source: Qt.resolvedUrl("CryptoPopup.qml")
        payload: ({
            ticks: root.configuredTicks,
            activeIndex: root.activeIndex,
            price: root.price,
            changePct: root.changePct
        })
        popupWidth: 624
        popupHeight: 432
        gap: 4
        placement: "below"
        horizontalAlignment: "center"
        keyboardFocus: true
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.toggle()
        onWheel: function(wheel) {
            root.setActiveIndex(root.activeIndex + (wheel.angleDelta.y < 0 ? 1 : -1))
            wheel.accepted = true
        }
    }
}
