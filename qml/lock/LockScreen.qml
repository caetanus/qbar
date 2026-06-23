import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root

    readonly property var screenStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lockscreen") : ({})
    readonly property var panelStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("auth-panel") : ({})
    readonly property var inputStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("password-input") : ({})
    readonly property var labelStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lock-label") : ({})
    readonly property var errorStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lock-error") : ({})

    function color(style, name, fallback) {
        return style && style[name] ? cssTheme.parseColor(style[name]) : fallback
    }

    QBar.CssFill {
        anchors.fill: parent
        style: root.screenStyle
        defaultColor: "#0b1020"
    }

    Rectangle {
        id: shade
        anchors.fill: parent
        color: root.color(root.screenStyle, "overlay-color", "transparent")
    }

    Item {
        id: panel
        width: Math.min(420, root.width - 48)
        height: 248
        anchors.centerIn: parent

        QBar.CssFill {
            anchors.fill: parent
            style: root.panelStyle
            radius: 12
            defaultColor: "#171d2d"
            defaultBorderColor: "#3d4861"
            defaultBorderWidth: 1
        }

        Column {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 12

            Text {
                width: parent.width
                color: root.color(root.labelStyle, "color", "#f4f7ff")
                font.family: root.labelStyle["font-family"] || "Inter"
                font.pixelSize: 24
                horizontalAlignment: Text.AlignHCenter
                text: Qt.formatDateTime(new Date(), "HH:mm")
            }

            Text {
                width: parent.width
                color: root.color(root.labelStyle, "color", "#f4f7ff")
                opacity: 0.88
                font.family: root.labelStyle["font-family"] || "Inter"
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                text: lockController.user
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                color: root.color(root.labelStyle, "color", "#f4f7ff")
                opacity: 0.72
                font.family: root.labelStyle["font-family"] || "Inter"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                text: lockController.prompt
                wrapMode: Text.WordWrap
            }

            Rectangle {
                width: parent.width
                height: 42
                radius: inputStyle["border-radius"] ? cssTheme.parseLength(inputStyle["border-radius"], 8) : 8
                color: root.color(root.inputStyle, "background-color", "#0c111e")
                border.color: root.color(root.inputStyle, "border-color", "#556178")
                border.width: inputStyle["border-width"] ? cssTheme.parseLength(inputStyle["border-width"], 1) : 1

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: passwordInput.forceActiveFocus()
                }

                TextInput {
                    id: passwordInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    color: root.color(root.inputStyle, "color", "#ffffff")
                    selectedTextColor: "#ffffff"
                    selectionColor: "#3f8cff"
                    echoMode: TextInput.Password
                    focus: true
                    cursorVisible: activeFocus
                    font.pixelSize: 16
                    verticalAlignment: TextInput.AlignVCenter
                    onAccepted: {
                        var password = text
                        text = ""
                        lockController.submitPassword(password)
                    }
                }
            }

            Text {
                width: parent.width
                height: 20
                color: root.color(root.errorStyle, "color", "#ff8585")
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                text: lockController.error.length > 0 ? lockController.error : lockController.message
                elide: Text.ElideRight
            }
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            lockController.cancel()
            event.accepted = true
        }
    }

    Component.onCompleted: passwordInput.forceActiveFocus()
}
