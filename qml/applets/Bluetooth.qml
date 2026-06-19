import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "bluetooth"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property bool available: bluetoothModel ? bluetoothModel.available : false
    property bool powered: bluetoothModel ? bluetoothModel.powered : false
    property int connectedCount: bluetoothModel ? bluetoothModel.connectedCount : 0
    property string displayText: bluetoothModel ? bluetoothModel.displayText : ""
    property string tooltipText: bluetoothModel ? bluetoothModel.tooltipText : "bluetooth unavailable"
    property bool tooltipHovered: false
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    // theme.* colours are HexArgb STRINGS, so `theme.foreground.r` is undefined and
    // Qt.rgba(undefined,...) renders black. Parse first, then tint.
    function alphaColor(colorStr, a) {
        var c = cssTheme.parseColor(colorStr)
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    readonly property color iconColor: {
        if (cssStyle["color"]) return cssTheme.parseColor(cssStyle["color"])
        if (!powered) return root.alphaColor(theme.foreground, 0.45)
        return connectedCount > 0 ? theme.accent : theme.foreground
    }

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    // Right-click menu: toggle power and connect/disconnect paired devices.
    function buildMenu() {
        var items = []
        items.push({ text: root.powered ? "Turn Bluetooth off" : "Turn Bluetooth on", action: "power" })

        if (root.powered) {
            items.push({ separator: true })
            var devices = bluetoothModel ? bluetoothModel.devices : []
            if (devices.length === 0) {
                items.push({ text: "No paired devices", enabled: false })
            } else {
                for (var i = 0; i < devices.length; i++) {
                    var d = devices[i]
                    items.push({
                        text: d.name,
                        iconName: d.iconName,
                        checkable: true,
                        checked: d.connected,
                        action: "device",
                        path: d.path
                    })
                }
            }
        }
        return items
    }

    QBar.MenuPopup {
        id: btMenu
        anchorItem: root
        gap: 4
        onTriggered: function(index, item) {
            if (!bluetoothModel) {
                return
            }
            if (item.action === "power") {
                bluetoothModel.togglePower()
            } else if (item.action === "device") {
                if (item.checked) {
                    bluetoothModel.disconnectDevice(item.path)
                } else {
                    bluetoothModel.connectDevice(item.path)
                }
            }
        }
    }

    // Background painted by the CssRect base.

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.available

        Item {
            width: 11
            height: 16
            anchors.verticalCenter: parent.verticalCenter

            QBar.CssIcon {
                id: cssIcon
                anchors.fill: parent
                style: root.cssStyle
                color: root.iconColor
                visible: hasCustomIcon
            }

            Canvas {
                id: icon
                anchors.fill: parent
                visible: !cssIcon.hasCustomIcon
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = root.iconColor
                    ctx.lineWidth = 1.4
                    ctx.lineJoin = "round"
                    ctx.lineCap = "round"

                    // The Bluetooth rune.
                    var x0 = 1.5, x1 = 9.5, midX = 5.5
                    var top = 1.5, bot = 14.5, q1 = 4.7, q3 = 11.3
                    ctx.beginPath()
                    ctx.moveTo(midX, top)
                    ctx.lineTo(midX, bot)
                    ctx.lineTo(x1, q3)
                    ctx.lineTo(x0, q1)
                    ctx.moveTo(midX, top)
                    ctx.lineTo(x1, q1)
                    ctx.lineTo(x0, q3)
                    ctx.stroke()
                }
                Connections {
                    target: root
                    function onIconColorChanged() { icon.requestPaint() }
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.connectedCount > 0
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
            text: root.connectedCount
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                btMenu.model = root.buildMenu()
                btMenu.toggle()
            } else if (bluetoothModel) {
                bluetoothModel.togglePower()
            }
        }
    }
}
