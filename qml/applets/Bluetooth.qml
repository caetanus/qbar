import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "bluetooth"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    property bool available: bluetoothModel ? bluetoothModel.available : false
    property bool powered: bluetoothModel ? bluetoothModel.powered : false
    property int connectedCount: bluetoothModel ? bluetoothModel.connectedCount : 0
    property string displayText: bluetoothModel ? bluetoothModel.displayText : ""
    property string tooltipText: bluetoothModel ? bluetoothModel.tooltipText : "bluetooth unavailable"
    property bool tooltipHovered: false
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    readonly property color iconColor: {
        if (cssStyle["color"]) return cssTheme.parseColor(cssStyle["color"])
        if (!powered) return Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.45)
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

    Rectangle {
        anchors.fill: parent
        visible: root.available
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "transparent"
    }

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

            Canvas {
                id: icon
                anchors.fill: parent
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
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: if (bluetoothModel) bluetoothModel.togglePower()
    }
}
