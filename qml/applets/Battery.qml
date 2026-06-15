import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    width: 58
    height: theme.height

    readonly property string cssId: "battery"
    readonly property var cssClasses: charging ? ["charging"] : full ? ["full"] : lowBattery ? ["critical"] : []
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId, cssClasses) : ({})

    property int level: batteryModel ? batteryModel.capacity : 0
    property bool charging: batteryModel ? batteryModel.charging : false
    property bool full: batteryModel ? batteryModel.full : false
    property bool lowBattery: batteryModel ? (!charging && !full && level <= 20) : false
    property real alertPulse: 0

    signal activated()

    function mixColor(a, b, t) {
        return Qt.rgba(
            a.r * (1 - t) + b.r * t,
            a.g * (1 - t) + b.g * t,
            a.b * (1 - t) + b.b * t,
            a.a * (1 - t) + b.a * t
        )
    }

    function backgroundColor() {
        if (lowBattery) {
            var darkEnd = cssStyle["background-color"]
                ? cssTheme.parseColor(cssStyle["background-color"])
                : Qt.rgba(0.17, 0.19, 0.20, 1.0)
            return mixColor(darkEnd, Qt.rgba(0.90, 0.16, 0.16, 1.0), alertPulse)
        }
        return cssStyle["background-color"]
            ? cssTheme.parseColor(cssStyle["background-color"])
            : (charging ? "#218f4f" : full ? "#ffffff" : "#2b2f33")
    }

    function contentColor() {
        if (lowBattery) {
            return mixColor(Qt.rgba(0.90, 0.16, 0.16, 1.0), Qt.rgba(1.0, 1.0, 1.0, 1.0), alertPulse)
        }
        return cssStyle["color"]
            ? cssTheme.parseColor(cssStyle["color"])
            : (full ? "#000000" : "#ffffff")
    }

    NumberAnimation on alertPulse {
        from: 0
        to: 1
        duration: 10000
        loops: Animation.Infinite
        running: root.lowBattery
        easing.type: Easing.InOutSine
    }

    onLowBatteryChanged: {
        if (!lowBattery) {
            alertPulse = 0
        }
    }

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

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor()
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
            color: root.contentColor()
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
                color: root.contentColor()
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
        color: root.contentColor()
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
