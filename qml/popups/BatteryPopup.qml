import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    width: 280
    implicitWidth: 280
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight

    property var battery: null
    property bool lowBattery: battery ? (!battery.charging && !battery.full && battery.capacity <= 20) : false
    property real alertPulse: 0
    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property var batteryPopupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("battery-popup") : ({})
    readonly property color popupForeground: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color popupForegroundSoft: Qt.rgba(popupForeground.r, popupForeground.g, popupForeground.b, 0.76)
    readonly property color trackColor: Qt.rgba(popupForeground.r, popupForeground.g, popupForeground.b, 0.13)
    readonly property color dividerColor: Qt.rgba(popupForeground.r, popupForeground.g, popupForeground.b, 0.16)
    readonly property var popupTextShadow: cssTheme && cssTheme.loaded
        ? cssTheme.parseBoxShadow((batteryPopupStyle["text-shadow"] || popupStyle["text-shadow"] || "")) : ({})

    function hasBatteryPopupChrome() {
        return batteryPopupStyle["background"] || batteryPopupStyle["background-color"]
            || batteryPopupStyle["background-image"] || batteryPopupStyle["border-color"]
            || batteryPopupStyle["box-shadow"]
    }

    component PopupText: Text {
        color: root.popupForeground
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        layer.enabled: root.popupTextShadow.color !== undefined
        layer.effect: QBar.CssDropShadow { shadow: root.popupTextShadow }
    }

    component DetailRow: Row {
        property string label: ""
        property string value: ""
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4

        PopupText {
            text: parent.label
            color: root.popupForegroundSoft
        }

        PopupText {
            text: parent.value
        }
    }

    function formatDuration(seconds) {
        if (!seconds || seconds <= 0) {
            return "--"
        }

        var totalMinutes = Math.floor(seconds / 60)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
        if (hours <= 0) {
            return minutes + "m"
        }
        return hours + "h " + minutes + "m"
    }

    function statusLabel() {
        if (!battery) {
            return "--"
        }
        if (battery.charging) {
            return qsTr("Charging")
        }
        if (battery.full) {
            return qsTr("Charged")
        }
        if (battery.discharging) {
            return qsTr("Discharging")
        }
        return battery.status
    }

    function timeLabel() {
        if (!battery) {
            return "--"
        }
        if (battery.charging) {
            return qsTr("Time to full:")
        }
        return qsTr("Time to empty:")
    }

    function statusColor() {
        if (!battery) {
            return root.popupForeground
        }
        if (lowBattery) {
            return mixColor(Qt.rgba(0.90, 0.16, 0.16, 1.0), root.popupForeground, alertPulse * 0.28)
        }
        if (battery.charging) {
            return "#22c55e"
        }
        if (battery.full) {
            return root.popupForeground
        }
        return "#cbd5e1"
    }

    function mixColor(a, b, t) {
        return Qt.rgba(
            a.r * (1 - t) + b.r * t,
            a.g * (1 - t) + b.g * t,
            a.b * (1 - t) + b.b * t,
            a.a * (1 - t) + b.a * t
        )
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

    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    QBar.CssFill {
        anchors.fill: parent
        visible: root.hasBatteryPopupChrome()
        style: root.batteryPopupStyle
        radius: batteryPopupStyle["border-radius"] ? parseFloat(batteryPopupStyle["border-radius"]) : 4
        defaultColor: "transparent"
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8

            PopupText {
                text: root.statusLabel()
                color: root.statusColor()
                font.pointSize: theme.fontSize + 1
                font.bold: true
            }

            PopupText {
                text: battery ? battery.capacity + "%" : ""
                font.pointSize: theme.fontSize + 1
                font.bold: true
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            radius: 4
            color: root.trackColor

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * (battery ? battery.capacity / 100.0 : 0)
                radius: 4
                color: root.statusColor()
                Behavior on width {
                    NumberAnimation {
                        duration: 250
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: root.dividerColor
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 6

            DetailRow {
                visible: battery ? battery.timeRemainingAvailable && battery.timeRemaining > 0 : false
                label: root.timeLabel()
                value: root.formatDuration(battery ? battery.timeRemaining : 0)
            }

            DetailRow {
                visible: battery ? battery.cyclesAvailable : false
                label: qsTr("Cycles:")
                value: battery ? battery.cycles : ""
            }

            DetailRow {
                visible: battery ? battery.healthAvailable : false
                label: qsTr("Health:")
                value: battery ? battery.health + "%" : ""
            }

            DetailRow {
                visible: battery ? battery.energyRateAvailable : false
                label: qsTr("Power draw:")
                value: battery ? battery.energyRate.toFixed(1) + " W" : ""
            }
        }
    }
}
