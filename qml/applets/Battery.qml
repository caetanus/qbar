import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "battery"
    // State → cssClass: the engine renders #battery / .charging / .full / .critical.
    cssClass: charging ? ["charging"] : full ? ["full"] : lowBattery ? ["critical"] : []
    defaultColor: "#2b2f33"
    width: 58
    height: theme.height

    readonly property var cssStyle: root.style

    property int level: batteryModel ? batteryModel.capacity : 0
    property bool charging: batteryModel ? batteryModel.charging : false
    property bool full: batteryModel ? batteryModel.full : false
    property bool lowBattery: batteryModel ? (!charging && !full && level <= 20) : false

    signal activated()

    // Icon/text colour: themed #battery[.state] color, else a sensible contrast default
    // (white over the dark/critical base, black over a light "full" badge).
    readonly property color contentColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : (full ? "#000000" : "#ffffff")

    // Critical pulse via standard CSS `@keyframes` on `#battery.critical::before` — a red
    // overlay whose opacity pulses. Replaces the old QML alertPulse colour-mix.
    readonly property var pulseStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("battery", ["critical"], "before") : ({})
    readonly property var pulseAnim: cssTheme && cssTheme.loaded
        ? cssTheme.parseAnimation(pulseStyle["animation"] || "") : ({})
    readonly property var pulseFrames: cssTheme && cssTheme.loaded && pulseAnim.name
        ? cssTheme.keyframes(pulseAnim.name) : []
    readonly property bool pulseEnabled: lowBattery && pulseFrames.length > 0

    QBar.Popup {
        id: batteryPopup
        anchorItem: root
        source: "qrc:/popups/BatteryPopup.qml"
        payload: ({ battery: batteryModel })
        width: 280
        height: 0
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    // Base background painted by the CssRect base (per-state via cssClass).

    // Critical pulse: a red overlay whose opacity is driven through the theme's
    // @keyframes; above the base fill, below the icon/text.
    Rectangle {
        id: pulseOverlay
        anchors.fill: parent
        color: {
            var bg = root.pulseStyle["background"] || root.pulseStyle["background-color"]
            return bg ? cssTheme.parseColor(bg) : "#e62929"
        }
        opacity: 0.0
        visible: root.pulseEnabled

        QBar.CssKeyframes {
            target: pulseOverlay
            animatedProperty: "opacity"
            frames: root.pulseFrames
            duration: root.pulseAnim.duration > 0 ? root.pulseAnim.duration : 1800
            easingType: root.pulseAnim.easing !== undefined ? root.pulseAnim.easing : Easing.InOutSine
            iterations: root.pulseAnim.iterations !== undefined ? root.pulseAnim.iterations : -1
            running: root.pulseEnabled
        }
    }

    Item {
        id: batteryIcon
        width: 12
        height: 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 4

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.topMargin: 3
            radius: 2
            border.width: 0
            color: "#00000000"
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 2
            width: 4
            height: 3
            radius: 0
            color: root.contentColor
            opacity: 0.65
        }

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 3
            anchors.bottomMargin: 2

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 2
                anchors.rightMargin: 2
                height: Math.max(2, Math.round(parent.height * root.level / 100))
                radius: 1
                color: root.contentColor
                opacity: 0.65
                Behavior on height {
                    NumberAnimation {
                        duration: 180
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }

        Canvas {
            anchors.centerIn: parent
            width: 8
            height: 12
            visible: root.charging
            onVisibleChanged: if (visible) requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.fillStyle = "#ffffff"
                ctx.beginPath()
                ctx.moveTo(4, 0)
                ctx.lineTo(1, 6)
                ctx.lineTo(4, 6)
                ctx.lineTo(2, 12)
                ctx.lineTo(7, 5)
                ctx.lineTo(4, 5)
                ctx.closePath()
                ctx.fill()
            }
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: batteryIcon.left
        anchors.rightMargin: 3
        color: root.contentColor
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: batteryModel ? level + "%" : "--"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: batteryPopup.toggle()
    }
}
