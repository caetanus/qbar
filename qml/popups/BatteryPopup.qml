import QtQuick

Item {
    id: root
    width: 280
    implicitWidth: 280
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight

    property var battery: null
    property bool lowBattery: battery ? (!battery.charging && !battery.full && battery.capacity <= 20) : false
    property real alertPulse: 0

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
            return "Charging"
        }
        if (battery.full) {
            return "Charged"
        }
        if (battery.discharging) {
            return "Discharging"
        }
        return battery.status
    }

    function timeLabel() {
        if (!battery) {
            return "--"
        }
        if (battery.charging) {
            return "Time to full:"
        }
        return "Time to empty:"
    }

    function statusColor() {
        if (!battery) {
            return "#ffffff"
        }
        if (lowBattery) {
            return mixColor(Qt.rgba(0.90, 0.16, 0.16, 1.0), Qt.rgba(1.0, 1.0, 1.0, 1.0), alertPulse)
        }
        if (battery.charging) {
            return "#218f4f"
        }
        if (battery.full) {
            return "#ffffff"
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
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.92)
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
        border.width: 1
        radius: 4
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

            Text {
                text: root.statusLabel()
                color: root.statusColor()
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 1
                font.bold: true
            }

            Text {
                text: battery ? battery.capacity + "%" : ""
                color: "#ffffff"
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 1
                font.bold: true
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            radius: 4
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.08)

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
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.1)
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 6

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 4
                visible: battery ? battery.timeRemainingAvailable && battery.timeRemaining > 0 : false

                Text {
                    text: root.timeLabel()
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }

                Text {
                    text: root.formatDuration(battery ? battery.timeRemaining : 0)
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 4
                visible: battery ? battery.cyclesAvailable : false

                Text {
                    text: "Cycles:"
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }

                Text {
                    text: battery ? battery.cycles : ""
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 4
                visible: battery ? battery.healthAvailable : false

                Text {
                    text: "Health:"
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }

                Text {
                    text: battery ? battery.health + "%" : ""
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 4
                visible: battery ? battery.energyRateAvailable : false

                Text {
                    text: "Power draw:"
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }

                Text {
                    text: battery ? battery.energyRate.toFixed(1) + " W" : ""
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }
    }
}
