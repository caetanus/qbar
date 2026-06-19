import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "backlight"
    defaultColor: "#4b5563"
    height: theme.height
    width: 58

    readonly property var cssStyle: root.style

    property int brightnessValue: brightnessModel ? brightnessModel.percent : 0
    property bool available: brightnessModel ? brightnessModel.available : false
    property real illumination: root.available ? root.brightnessValue / 100.0 : 0.0
    readonly property color cutColor: cssStyle["background-color"]
        ? cssTheme.parseColor(cssStyle["background-color"])
        : "#4b5563"

    // Background painted by the CssRect base (defaultColor fallback); the moon's cut uses
    // the same cutColor so it blends with that background.

    Canvas {
        id: moonCanvas
        width: 16
        height: 16
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 7
        renderTarget: Canvas.Image

        onPaint: {
            const ctx = getContext("2d")
            const cx = width / 2
            const cy = height / 2
            const radius = Math.min(width, height) / 2 - 1
            const phase = Math.max(0, Math.min(1, root.illumination))
            const cutX = cx + (radius * 2 * phase)
            const cutRadius = radius * 0.98

            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = cssStyle["color"] || "#ffffff"
            ctx.beginPath()
            ctx.arc(cx, cy, radius, 0, Math.PI * 2)
            ctx.fill()

            ctx.beginPath()
            ctx.arc(cutX, cy, cutRadius, 0, Math.PI * 2)
            if (root.cutColor.a <= 0.01) {
                ctx.globalCompositeOperation = "destination-out"
                ctx.fillStyle = "#000000"
                ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            } else {
                ctx.fillStyle = root.cutColor
                ctx.fill()
            }
        }

        onVisibleChanged: if (visible) requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
        Connections {
            target: brightnessModel
            function onBrightnessChanged() { moonCanvas.requestPaint() }
            function onAvailabilityChanged() { moonCanvas.requestPaint() }
        }
        Connections {
            target: root
            function onCssStyleChanged() { moonCanvas.requestPaint() }
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 7
        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#ffffff"
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
        text: root.available ? root.brightnessValue + "%" : "--"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
        onWheel: function(wheel) {
            if (!brightnessModel || !root.available) {
                return
            }
            if (wheel.angleDelta.y > 0) {
                brightnessModel.stepUp(5)
            } else if (wheel.angleDelta.y < 0) {
                brightnessModel.stepDown(5)
            }
        }
    }
}
