import QtQuick
import "qrc:/qbar" as QBar

// Peripheral battery levels (waybar's "upower" module). Two display modes via the module
// "/variant":
//   UPower      → a single device (the LOWEST charge by default); scroll cycles through all
//                 devices (sorted by charge, ending at the highest); click opens a popup with
//                 every device.
//   UPower/all  → every device inline (handy inside a drawer).
// Shows a device-kind emoji + the shared QBar.BatteryIcon + percentage. Hides (width 0) when
// no peripheral batteries are present.
QBar.CssRect {
    id: root
    cssId: "upower"
    height: theme.height
    width: Math.max(1, preferredWidth)

    property string variant: ""
    readonly property bool showAll: variant === "all"
    readonly property var cssStyle: root.style
    readonly property var devices: upowerModel ? upowerModel.devices : []
    readonly property bool available: devices.length > 0
    property bool tooltipHovered: false

    // Ascending by charge → index 0 is the lowest (what single mode shows first).
    readonly property var sortedDevices: {
        var arr = devices.slice()
        arr.sort(function (a, b) { return a.percentage - b.percentage })
        return arr
    }
    property int currentIndex: 0
    readonly property int clampedIndex: sortedDevices.length > 0
        ? Math.max(0, Math.min(currentIndex, sortedDevices.length - 1)) : 0
    readonly property var currentDevice: sortedDevices.length > 0 ? sortedDevices[clampedIndex] : null

    readonly property color baseColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
    readonly property color lowColor: cssStyle["color"] ? root.baseColor : "#e64553"
    readonly property color chargingColor: "#40a02b"

    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 10) : 0

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function kindGlyph(kind) {
        switch (kind) {
        case "mouse": return "🖱"
        case "keyboard": return "⌨"
        case "headset":
        case "headphones":
        case "speakers": return "🎧"
        case "gaming-input": return "🎮"
        case "phone": return "📱"
        case "tablet": return "▭"
        case "media-player": return "🎵"
        default: return "🔋"
        }
    }
    function devColor(d) {
        return d.isCharging ? chargingColor : (d.percentage <= 20 ? lowColor : baseColor)
    }

    QBar.Popup {
        id: popup
        name: "upower"
        anchorItem: root
        source: "qrc:/popups/UPowerPopup.qml"
        payload: ({ upower: upowerModel })
        width: 260
        height: 0
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered && !popup.isOpen
        text: upowerModel ? upowerModel.tooltipText : ""
        side: "auto"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 5
        spacing: 9
        visible: root.available

        Repeater {
            // all mode: every device; single mode: only the current (lowest by default).
            model: root.showAll ? root.sortedDevices : (root.currentDevice ? [root.currentDevice] : [])

            delegate: Row {
                id: dev
                required property var modelData
                spacing: 3

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.kindGlyph(dev.modelData.kind)
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }
                QBar.BatteryIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 11
                    height: 16
                    level: dev.modelData.percentage
                    charging: dev.modelData.isCharging
                    color: root.devColor(dev.modelData)
                    fillOpacity: 0.9
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: dev.modelData.percentage + "%"
                    color: root.devColor(dev.modelData)
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: if (root.available) popup.toggle()
        onWheel: function (wheel) {
            // Cycle the shown device only in single mode (sorted ascending → ends at highest).
            if (root.showAll || root.sortedDevices.length < 2)
                return
            var n = root.sortedDevices.length
            root.currentIndex = wheel.angleDelta.y > 0
                ? (root.clampedIndex - 1 + n) % n
                : (root.clampedIndex + 1) % n
        }
    }
}
