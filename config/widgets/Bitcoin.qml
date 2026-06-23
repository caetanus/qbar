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

    function refresh() {
        Fetch.fetch("https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT")
            .then((r) => r.json())
            .then((d) => {
                root.price = parseFloat(d.lastPrice)
                root.changePct = parseFloat(d.priceChangePercent)
            })
            .catch((e) => {
                console.warn("btc widget: ticker fetch failed:", e)
            })
    }

    Component.onCompleted: {
        preferredWidthUpdated(preferredWidth)
        refresh()
    }

    Timer {
        interval: 30000
        running: true
        repeat: true
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
            text: root.price > 0
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
