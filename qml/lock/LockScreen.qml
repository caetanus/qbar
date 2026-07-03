import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar

Item {
    id: root

    readonly property var screenStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lockscreen") : ({})
    readonly property var panelStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("auth-panel") : ({})
    readonly property var inputStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("password-input") : ({})
    readonly property var labelStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lock-label") : ({})
    readonly property var errorStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lock-error") : ({})

    readonly property string displayName: (typeof userModel !== "undefined" && userModel && userModel.realName.length)
        ? userModel.realName : lockController.user
    readonly property string avatarSource: (typeof lockHideAvatar !== "undefined" && lockHideAvatar) ? ""
        : (typeof userModel !== "undefined" && userModel && userModel.iconPath)
        ? userModel.iconPath : ""

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
        height: col.implicitHeight + 56
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
            id: col
            width: parent.width - 56
            anchors.top: parent.top
            anchors.topMargin: 28
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12

            // Avatar (AccountsService / ~/.face), circular. Falls back to a monogram disc.
            Item {
                width: 72
                height: 72
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "#232a3d"
                    border.color: "#3d4861"
                    border.width: 1
                    // Hide the whole fallback disc (and its border) once the avatar image
                    // loads, so nothing bleeds around the masked photo.
                    opacity: avatarImg.status === Image.Ready ? 0 : 1
                    Text {
                        anchors.centerIn: parent
                        text: root.displayName.length ? root.displayName[0].toUpperCase() : "?"
                        color: "#cfd8ec"
                        font.pixelSize: 28
                    }
                }
                Image {
                    id: avatarImg
                    anchors.fill: parent
                    source: root.avatarSource
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 144
                    sourceSize.height: 144
                    visible: false
                }
                MultiEffect {
                    anchors.fill: parent
                    source: avatarImg
                    maskEnabled: true
                    maskSource: avatarMask
                    opacity: avatarImg.status === Image.Ready ? 1 : 0
                }
                Item {
                    id: avatarMask
                    anchors.fill: parent
                    layer.enabled: true
                    visible: false
                    Rectangle { anchors.fill: parent; radius: width / 2; color: "black" }
                }
            }

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
                text: root.displayName
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
                id: inputBox
                width: parent.width
                height: 42
                radius: inputStyle["border-radius"] ? cssTheme.parseLength(inputStyle["border-radius"], 8) : 8
                color: root.color(root.inputStyle, "background-color", "#0c111e")
                // A failed attempt is LOUD: the box shakes (i3lock-style, same cadence as
                // the ring face) and the border holds the error red until the next try.
                border.color: lockController.error.length > 0
                    ? root.color(root.errorStyle, "color", "#ff8585")
                    : root.color(root.inputStyle, "border-color", "#556178")
                border.width: inputStyle["border-width"] ? cssTheme.parseLength(inputStyle["border-width"], 1) : 1
                Behavior on border.color { ColorAnimation { duration: 180 } }

                property real shake: 0
                transform: Translate { x: inputBox.shake }

                SequentialAnimation {
                    id: shakeAnim
                    NumberAnimation { target: inputBox; property: "shake"; to: -14; duration: 50 }
                    NumberAnimation { target: inputBox; property: "shake"; to: 14; duration: 90 }
                    NumberAnimation { target: inputBox; property: "shake"; to: -8; duration: 70 }
                    NumberAnimation { target: inputBox; property: "shake"; to: 0; duration: 60 }
                }

                Connections {
                    target: lockController
                    function onErrorChanged() {
                        if (lockController.error.length > 0)
                            shakeAnim.restart()
                    }
                }

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
                    // TextInput paints past its bounds by default — without this a long
                    // password's echo dots spill out of the box.
                    clip: true
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

            // Real auth errors are red; method hints (fingerprint/face) reuse this line
            // but must NOT look like errors — show them in the normal label colour.
            Text {
                width: parent.width
                height: 20
                color: lockController.error.length > 0
                    ? root.color(root.errorStyle, "color", "#ff8585")
                    : root.color(root.labelStyle, "color", "#f4f7ff")
                opacity: lockController.error.length > 0 ? 1.0 : 0.72
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                text: lockController.error.length > 0 ? lockController.error : lockController.message
                elide: Text.ElideRight
            }

            // Caps / Num Lock — Caps ON is a loud red warning (mistyped-password guard).
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 18
                Text {
                    text: keyLocks.active ? "CAPS LOCK ON" : "Caps Lock"
                    color: keyLocks.active ? "#f7768e" : "#5b6272"
                    font.bold: keyLocks.active
                    font.pixelSize: 12
                }
                Text {
                    text: keyLocks.numLockActive ? "Num Lock On" : "Num Lock Off"
                    color: keyLocks.numLockActive ? "#9ece6a" : "#5b6272"
                    font.pixelSize: 12
                }
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
