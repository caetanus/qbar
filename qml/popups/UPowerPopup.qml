import QtQuick
import "qrc:/qbar" as QBar

// All peripheral batteries, one row each (battery glyph + device name + percentage). Fed the
// live UpowerModel via payload, so it updates while open.
Item {
    id: root

    property var upower: null
    readonly property var devices: upower ? upower.devices : []
    readonly property var sorted: {
        var a = devices.slice()
        a.sort(function (x, y) { return x.percentage - y.percentage })
        return a
    }

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color lowColor: "#e64553"
    readonly property color chargingColor: "#40a02b"

    function devColor(d) {
        return d.isCharging ? chargingColor : (d.percentage <= 20 ? lowColor : fg)
    }

    implicitWidth: 260
    width: implicitWidth
    implicitHeight: col.implicitHeight + 24
    height: implicitHeight

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 7

        Text {
            text: root.devices.length === 1 ? "Peripheral battery" : "Peripheral batteries"
            color: root.fg
            font.bold: true
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
        }

        Repeater {
            model: root.sorted

            delegate: Item {
                required property var modelData
                width: col.width
                height: 22

                QBar.BatteryIcon {
                    id: bi
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: 18
                    level: modelData.percentage
                    charging: modelData.isCharging
                    color: root.devColor(modelData)
                    fillOpacity: 0.9
                }
                Text {
                    id: pctT
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.percentage + "%" + (modelData.isCharging ? " ⚡" : "")
                    color: root.devColor(modelData)
                    font.bold: true
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
                Text {
                    anchors.left: bi.right
                    anchors.leftMargin: 9
                    anchors.right: pctT.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.model && modelData.model.length > 0 ? modelData.model : modelData.kind
                    color: root.fg
                    elide: Text.ElideRight
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }
    }
}
