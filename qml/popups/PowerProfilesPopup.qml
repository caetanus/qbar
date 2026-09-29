import QtQuick
import "qrc:/qbar" as QBar

// Power profile picker (power-profiles-daemon) + the CPU boost state the
// daemon set (read-only: ppd has no boost API; it ties boost to the profile
// and, on laptops, to battery/AC).
// Fed the live PowerProfilesModel via payload, so it updates while open.
Item {
    id: root

    property var power: null
    readonly property var profiles: power ? power.profiles : []
    readonly property string active: power ? power.activeProfile : ""

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color accent: theme.accent !== undefined ? cssTheme.parseColor(theme.accent) : "#1e66f5"

    function profileLabel(p) {
        if (p === "performance") return qsTr("Performance")
        if (p === "balanced") return qsTr("Balanced")
        if (p === "power-saver") return qsTr("Power saver")
        return p
    }

    // The payload lands after the popup is created and sized; reserve the boost
    // row from the start (unless the model says boost is unsupported), since
    // the popup shell doesn't re-place itself when the content grows.
    readonly property bool showBoost: !power || power.boostSupported

    implicitWidth: 240
    width: implicitWidth
    // Computed from the data, not col.implicitHeight: the popup service reads
    // this synchronously right after assigning the payload, before Column has
    // re-laid out the freshly created profile rows (it does that on polish).
    readonly property int titleHeight: 24
    readonly property int rowHeight: 26
    readonly property int boostHeight: 32
    implicitHeight: {
        var h = titleHeight
        var items = 1
        h += profiles.length * rowHeight; items += profiles.length
        if (showBoost) { h += 1 + boostHeight; items += 2 }
        return 24 + h + (items - 1) * col.spacing
    }
    height: implicitHeight

    // sysfs has no change notification, and the daemon can flip boost without a
    // profile change (battery-aware on laptops: AC plug/unplug). Re-read while
    // the popup is showing; the shell is reused across opens, so key off
    // visibility rather than creation.
    Timer {
        interval: 2000
        repeat: true
        triggeredOnStart: true
        running: root.visible && root.power !== null
        onTriggered: root.power.refreshBoost()
    }

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 4

        Text {
            text: qsTr("Power profile")
            color: root.fg
            font.bold: true
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
            height: root.titleHeight
            verticalAlignment: Text.AlignTop
        }

        Repeater {
            model: root.profiles

            delegate: Rectangle {
                required property string modelData
                readonly property bool selected: modelData === root.active
                width: col.width
                height: root.rowHeight
                radius: 5
                color: selected ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18)
                     : (rowMouse.containsMouse ? Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.08) : "transparent")

                // Radio indicator.
                Rectangle {
                    id: radio
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 12; radius: 6
                    color: "transparent"
                    border.width: 1.5
                    border.color: parent.selected ? root.accent : root.fgSoft
                    Rectangle {
                        anchors.centerIn: parent
                        width: 6; height: 6; radius: 3
                        color: root.accent
                        visible: parent.parent.selected
                    }
                }

                Text {
                    anchors.left: radio.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.profileLabel(modelData)
                    color: root.fg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.power && !parent.selected) root.power.setProfile(modelData)
                }
            }
        }

        Rectangle {
            width: col.width
            height: 1
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.15)
            visible: root.showBoost
        }

        // CPU boost, as the daemon set it.
        Item {
            width: col.width
            height: root.boostHeight
            visible: root.showBoost

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: qsTr("CPU boost")
                    color: root.fg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
                Text {
                    text: qsTr("set by power-profiles-daemon")
                    color: root.fgSoft
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 2
                }
            }

            Row {
                readonly property bool on: root.power ? root.power.boost : false
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: parent.on ? root.accent : root.fgSoft
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.on ? qsTr("On") : qsTr("Off")
                    color: parent.on ? root.fg : root.fgSoft
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }
    }
}
