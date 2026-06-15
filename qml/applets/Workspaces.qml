import QtQuick

Item {
    id: root
    height: theme.height
    width: preferredWidth

    property int preferredWidth: Math.ceil(contentRow.childrenRect.width)

    signal activated()
    signal workspaceActivated(string workspaceName)
    signal workspaceScrolled(int direction)
    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function scrollWorkspace(wheel) {
        if (wheel.angleDelta.y === 0) {
            return
        }

        root.workspaceScrolled(wheel.angleDelta.y < 0 ? 1 : -1)
        wheel.accepted = true
    }

    Row {
        id: contentRow
        spacing: 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Repeater {
            id: ipcRepeater
            model: workspaceModel && !workspaceModel.empty ? workspaceModel : 0

            Rectangle {
                id: workspaceTile
                width: label.implicitWidth + 8
                height: theme.height
                radius: 0
                clip: true
                property real hoverReveal: 0.0
                property real hoverOpacity: 0.0
                property bool attention: false
                property real activeProgress: focused ? 1.0 : 0.0

                // #workspaces button[.focused/.visible/.urgent] from the CSS theme
                readonly property var cssStyle: cssTheme && cssTheme.loaded
                    ? cssTheme.resolveWith("workspaces", "button",
                        urgent ? ["urgent"] : focused ? ["focused"] : visible ? ["visible"] : [])
                    : ({})
                readonly property color tileBg: cssStyle["background-color"]
                    ? cssTheme.parseColor(cssStyle["background-color"])
                    : (workspaceTile.attention ? "#ff5555" : visible ? "#526171" : "#273847")
                readonly property color tileFg: cssStyle["color"]
                    ? cssTheme.parseColor(cssStyle["color"])
                    : (workspaceTile.attention ? "#ffffff" : "#eef2f7")

                color: tileBg

                Behavior on color {
                    ColorAnimation {
                        duration: 180
                        easing.type: Easing.InOutQuad
                    }
                }

                Behavior on activeProgress {
                    NumberAnimation {
                        duration: 240
                        easing.type: Easing.InOutCubic
                    }
                }

                Timer {
                    id: hoverResetTimer
                    interval: 240
                    repeat: false
                    onTriggered: workspaceTile.hoverReveal = 0.0
                }

                Rectangle {
                    id: activeFill
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: (parent.height - height) / 2
                    width: parent.width
                    height: Math.max(2, parent.height * workspaceTile.activeProgress)
                    color: "#2f97d1"
                    opacity: workspaceTile.activeProgress
                    visible: workspaceTile.activeProgress > 0.0
                }

                Rectangle {
                    id: hoverLine
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: parent.width * workspaceTile.hoverReveal
                    height: 2
                    color: workspaceTile.attention ? "#ff5555" : theme.accent
                    opacity: workspaceTile.hoverOpacity

                    Behavior on width {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutCubic
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 240
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    color: workspaceTile.tileFg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    text: name
                }

                MouseArea {
                    id: workspaceMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.workspaceActivated(name)
                    onWheel: function(wheel) { root.scrollWorkspace(wheel) }
                    onContainsMouseChanged: {
                        if (containsMouse) {
                            hoverResetTimer.stop()
                            workspaceTile.hoverReveal = 1.0
                            workspaceTile.hoverOpacity = 1.0
                        } else {
                            workspaceTile.hoverOpacity = 0.0
                            hoverResetTimer.restart()
                        }
                    }
                }

            }
        }

        Repeater {
            id: fallbackRepeater
            model: workspaceModel && !workspaceModel.empty ? 0 : 5

            Rectangle {
                id: fallbackTile
                width: label.implicitWidth + 8
                height: theme.height
                radius: 0
                clip: true
                color: ["#273847", "#526171", "#ff5555", "#35495c", "#4a596a"][index]
                property real hoverReveal: 0.0
                property real hoverOpacity: 0.0

                Behavior on color {
                    ColorAnimation {
                        duration: 180
                        easing.type: Easing.InOutQuad
                    }
                }

                property bool attention: false

                Timer {
                    id: fallbackHoverResetTimer
                    interval: 240
                    repeat: false
                    onTriggered: fallbackTile.hoverReveal = 0.0
                }

                Rectangle {
                    id: fallbackHoverLine
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: parent.width * fallbackTile.hoverReveal
                    height: 2
                    color: fallbackTile.attention ? "#ff5555" : theme.accent
                    opacity: fallbackTile.hoverOpacity

                    Behavior on width {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutCubic
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 240
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    color: fallbackTile.attention ? "#ffffff" : "#eef2f7"
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    text: index === 4 ? "5:●" : (index + 1) + ":>"
                }

                MouseArea {
                    id: fallbackMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.workspaceActivated(String(index + 1))
                    onWheel: function(wheel) { root.scrollWorkspace(wheel) }
                    onContainsMouseChanged: {
                        if (containsMouse) {
                            fallbackHoverResetTimer.stop()
                            fallbackTile.hoverReveal = 1.0
                            fallbackTile.hoverOpacity = 1.0
                        } else {
                            fallbackTile.hoverOpacity = 0.0
                            fallbackHoverResetTimer.restart()
                        }
                    }
                }
            }
        }
    }
}
