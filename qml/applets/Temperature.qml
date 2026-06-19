import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "temperature"
    defaultColor: "#f28d26"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property string displayText: temperatureModel ? temperatureModel.displayText : "--/--"
    property string tooltipText: temperatureModel ? temperatureModel.tooltipText : "temperature unavailable"
    property bool available: temperatureModel ? temperatureModel.available : false
    property bool tooltipHovered: false
    property int preferredWidth: Math.ceil(contentRow.implicitWidth + 10)

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    // Background painted by the CssRect base (defaultColor fallback).

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5

        Item {
            width: 12
            height: 18
            anchors.verticalCenter: parent.verticalCenter

            QBar.CssIcon {
                id: cssIcon
                anchors.fill: parent
                style: root.cssStyle
                color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#ffffff"
                visible: hasCustomIcon
            }

            Canvas {
                anchors.fill: parent
                visible: !cssIcon.hasCustomIcon
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var iconColor = cssStyle["color"] || "#ffffff"
                    ctx.strokeStyle = iconColor
                    ctx.fillStyle = iconColor
                    ctx.lineWidth = 2.2
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"

                    ctx.beginPath()
                    ctx.moveTo(6, 3.0)
                    ctx.lineTo(6, 11.0)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.arc(6, 14.2, 3.1, 0, Math.PI * 2)
                    ctx.fill()
                }

                Connections {
                    target: root
                    function onCssStyleChanged() { parent.requestPaint() }
                }
            }
        }

        Text {
            id: valueLabel
            anchors.verticalCenter: parent.verticalCenter
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#ffffff"
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
            text: root.displayText
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
    }
}
