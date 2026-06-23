import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar

// User profile popup: large avatar + real name / login / uptime, plus the logged-in
// sessions (the `who` output). Fed the live UserModel via payload.
Item {
    id: root

    property var user: null

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color accent: (theme.accent !== undefined) ? cssTheme.parseColor(theme.accent) : "#63b3ed"
    readonly property bool hasFace: bigFace.status === Image.Ready

    implicitWidth: 340
    width: implicitWidth
    implicitHeight: col.implicitHeight + 24
    height: implicitHeight

    // Refresh `who` whenever the popup opens, regardless of trigger (click OR `qbar-ipc
    // toggle user`). The payload `user` is assigned AFTER Component.onCompleted (PopupShell
    // sets it in its Loader.onLoaded), so key off it arriving rather than completion.
    onUserChanged: if (user) user.refreshSessions()

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 12

        // ── header: avatar + names ──
        Row {
            spacing: 12

            Item {
                width: 52
                height: 52
                anchors.verticalCenter: parent.verticalCenter

                Image {
                    id: bigFace
                    anchors.fill: parent
                    source: root.user ? root.user.iconPath : ""
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                    visible: false
                    sourceSize.width: 104
                    sourceSize.height: 104
                }
                Rectangle {
                    id: bigMask
                    anchors.fill: parent
                    radius: width / 2
                    color: "black"
                    visible: false
                    layer.enabled: true
                }
                MultiEffect {
                    anchors.fill: parent
                    source: bigFace
                    maskEnabled: true
                    maskSource: bigMask
                    maskThresholdMin: 0.5
                    visible: root.hasFace
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    visible: !root.hasFace
                    color: root.accent
                    Text {
                        anchors.centerIn: parent
                        text: (root.user && root.user.userName.length > 0)
                            ? root.user.userName.charAt(0).toUpperCase() : "?"
                        color: "#ffffff"
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 8
                        font.bold: true
                    }
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                // Fill the remaining header width (popup content minus the avatar + spacing)
                // so a long real name elides instead of spilling past the popup edge.
                width: col.width - 52 - parent.spacing
                spacing: 2

                Text {
                    width: parent.width
                    text: root.user ? root.user.realName : ""
                    color: root.fg
                    font.bold: true
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize + 2
                    elide: Text.ElideRight
                    visible: text.length > 0
                }
                Text {
                    width: parent.width
                    text: root.user ? "@" + root.user.userName : ""
                    color: root.fgSoft
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    elide: Text.ElideRight
                }
                Text {
                    text: root.user ? "Up " + root.user.uptimeText : ""
                    color: root.fgSoft
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.12)
        }

        // ── sessions (who) ──
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Sessions"
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 1
            }
            Text {
                width: parent.width
                text: (root.user && root.user.sessionsText.length > 0) ? root.user.sessionsText : "…"
                color: root.fg
                font.family: "monospace"
                font.pointSize: theme.fontSize - 1
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }
    }
}
