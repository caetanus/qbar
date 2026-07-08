import QtQuick
import "qrc:/qbar" as QBar

// Failed systemd units, one row each: scope badge (system/user) + unit name +
// active/sub state. Fed the live FailedUnitsModel via payload.
Item {
    id: root

    property var failedUnits: null
    readonly property var units: failedUnits ? failedUnits.units : []

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color warnColor: "#e5c07b"
    readonly property color failColor: "#e64553"

    implicitWidth: 440
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
            text: root.units.length === 0
                ? qsTr("No failed units")
                : qsTr("Failed systemd units")
            color: root.fg
            font.bold: true
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
        }

        Repeater {
            model: root.units

            delegate: Item {
                required property var modelData
                width: col.width
                height: 24

                Rectangle {
                    id: scopeBadge
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: scopeText.implicitWidth + 12
                    height: 16
                    radius: 8
                    color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.12)

                    Text {
                        id: scopeText
                        anchors.centerIn: parent
                        text: modelData.scope
                        color: root.fgSoft
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize - 2
                    }
                }
                Text {
                    id: stateT
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.subState
                    color: root.failColor
                    font.bold: true
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }
                Text {
                    anchors.left: scopeBadge.right
                    anchors.leftMargin: 9
                    anchors.right: stateT.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    color: root.fg
                    elide: Text.ElideMiddle
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }

        Text {
            visible: root.units.length > 0
            text: qsTr("systemctl [--user] reset-failed clears the list")
            color: root.fgSoft
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize - 2
        }
    }
}
