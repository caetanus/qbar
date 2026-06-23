import QtQuick
import QtQuick.Effects
import QtCore
import "qrc:/qbar" as QBar

// waybar's "user" module: a round avatar (AccountsService, ~/.face fallback) + the user name
// and system uptime. Scroll cycles three display modes (avatar only → avatar+name →
// avatar+uptime); the tooltip always shows everything; click opens a popup with the profile
// and the logged-in sessions (`who`).
QBar.CssRect {
    id: root
    cssId: "user"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style
    readonly property color fg: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
    readonly property color accent: (theme.accent !== undefined) ? cssTheme.parseColor(theme.accent) : "#63b3ed"
    readonly property int avatarSize: Math.max(14, Math.round(theme.height * 0.62))
    readonly property bool hasFace: faceImage.status === Image.Ready
    property bool tooltipHovered: false

    // 0 = avatar only, 1 = avatar + name, 2 = avatar + uptime. Scroll cycles; persisted.
    property int displayMode: 0
    Settings {
        category: "userApplet"
        property alias displayMode: root.displayMode
    }

    property int preferredWidth: Math.ceil(contentRow.implicitWidth + 12)

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Popup {
        id: popup
        name: "user"
        anchorItem: root
        source: "qrc:/popups/UserPopup.qml"
        payload: ({ user: userModel })
        width: 340
        height: 0
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered && !popup.isOpen
        text: userModel ? userModel.tooltipText : ""
        side: "auto"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 6

        Item {
            id: avatar
            width: root.avatarSize
            height: root.avatarSize
            anchors.verticalCenter: parent.verticalCenter

            Image {
                id: faceImage
                anchors.fill: parent
                source: userModel ? userModel.iconPath : ""
                fillMode: Image.PreserveAspectCrop
                cache: true
                visible: false
                sourceSize.width: root.avatarSize * 2
                sourceSize.height: root.avatarSize * 2
            }
            // Circular mask for the avatar photo.
            Rectangle {
                id: faceMask
                anchors.fill: parent
                radius: width / 2
                color: "black"
                visible: false
                layer.enabled: true
            }
            MultiEffect {
                anchors.fill: parent
                source: faceImage
                maskEnabled: true
                maskSource: faceMask
                maskThresholdMin: 0.5
                visible: root.hasFace
            }
            // Fallback: an accent disc with the user's initial.
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                visible: !root.hasFace
                color: root.accent
                Text {
                    anchors.centerIn: parent
                    text: (userModel && userModel.userName.length > 0)
                        ? userModel.userName.charAt(0).toUpperCase() : "?"
                    color: "#ffffff"
                    font.family: theme.fontFamily
                    font.pointSize: Math.max(7, theme.fontSize - 2)
                    font.bold: true
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: userModel ? userModel.userName : ""
            color: root.fg
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
            visible: root.displayMode === 1
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: userModel ? userModel.uptimeText : ""
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.6)
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize - 1
            visible: root.displayMode === 2 && text.length > 0
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: popup.toggle()  // UserPopup refreshes `who` itself on open
        onWheel: function (wheel) {
            root.displayMode = wheel.angleDelta.y > 0
                ? (root.displayMode + 2) % 3   // up = previous
                : (root.displayMode + 1) % 3   // down = next
        }
    }
}
