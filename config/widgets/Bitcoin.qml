import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch

// BTC ticker — an EXTERNAL runtime widget (lives in ~/.config/qbar/widgets, NOT compiled
// into qbar). A QML port of the old btc.py waybar script: it hits Binance directly and
// shows ₿ ▲/▼ <price/1000> (<change>%); click opens a candlestick popup.
//
// Enable in ~/.config/qbar/config.json (overrides the script-based default):
//   "modules-right": [ "CustomTool:custom/btc" ],
//   "customTools": { "custom/btc": { "source": "widgets/Bitcoin.qml" } }
QBar.CssRect {
    id: root

    property string toolId: ""
    cssId: "custom-btc"            // styled by the theme's #custom-btc, like the script tool
    height: theme.height
    // Sizing contract: the bar's Loader has anchors.fill and resizes the item, which
    // CLEARS a `width:` binding — so Bar.appletWidth reads `preferredWidth`. Drive that
    // (content width), not `width`.
    property int preferredWidth: Math.max(1, content.implicitWidth + 14)
    width: Math.max(1, preferredWidth)

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

    property real price: 0
    property real changePct: 0
    readonly property bool up: changePct >= 0
    readonly property color tint: up ? "#33b864" : "#e8546a"

    // Resilience state: ride out transient network drops without flashing back to
    // "…", and retry fast (with backoff) until it recovers instead of waiting a
    // whole poll interval each time.
    property bool loaded: false        // got at least one good tick
    property bool stale: false         // last fetch failed but we still have a value
    property bool _inFlight: false     // a request is outstanding
    property bool _failed: false       // last attempt failed → use the fast retry cadence
    readonly property int _pollMs: 30000   // healthy poll interval
    readonly property int _retryMs: 5000   // while failing: keep retrying this often, forever

    function refresh() {
        if (root._inFlight)            // never pile up overlapping requests
            return
        root._inFlight = true
        Fetch.fetch("https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT", { timeout: 10000 })
            .then((r) => r.json())
            .then((d) => {
                root.price = parseFloat(d.lastPrice)
                root.changePct = parseFloat(d.priceChangePercent)
                root.loaded = true
                root.stale = false
                root._failed = false
            })
            .catch((e) => {
                console.warn("btc widget: ticker fetch failed:", e)
                root._failed = true
                if (root.loaded)
                    root.stale = true
            })
            // Runs after success OR a handled failure (.catch resolves to a value),
            // so it's our single "settled" hook. The next attempt is scheduled FROM
            // here (request completion), never on a free-running clock: on success,
            // poll again in 30s; while failing, keep retrying every 5s until it comes
            // back. A slow/hung request can't overlap the next tick.
            .then(() => {
                root._inFlight = false
                pollTimer.interval = root._failed ? root._retryMs : root._pollMs
                pollTimer.restart()
            })
    }

    Component.onCompleted: {
        preferredWidthUpdated(preferredWidth)
        refresh()
    }

    // Single-shot, re-armed by refresh() once each request settles — so a slow or
    // hung fetch can't overlap the next tick.
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
            text: "₿"
            color: "#f7931a" // bitcoin brand orange
            font.family: theme.fontFamily
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
            cssId: "custom-btc"
            anchors.verticalCenter: parent.verticalCenter
            // Keep showing the last good price during outages; dim it while stale so
            // it's visibly not-live, and show "…" only before the first good tick.
            opacity: root.stale ? 0.5 : 1.0
            text: root.loaded
                ? (root.price / 1000).toFixed(2) + " (" + root.changePct.toFixed(1) + "%)"
                : "…"
        }
    }

    QBar.Popup {
        id: popup
        name: "btc"   // reachable via the IPC: `qbar-ipc toggle btc`
        anchorItem: root
        source: Qt.resolvedUrl("BitcoinPopup.qml") // sibling external file
        payload: ({ price: root.price, changePct: root.changePct })
        popupWidth: 624
        popupHeight: 432
        gap: 4
        placement: "below"
        horizontalAlignment: "center"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.toggle()
    }
}
