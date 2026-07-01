import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar

// i3lock-style lock face: a solid-colour screen with a single central unlock ring.
// The ring pulses on each keystroke, spins while authenticating, flashes red on a
// failed attempt, and briefly turns green on success. There is no visible password
// field — keystrokes are captured invisibly, like i3lock.
Item {
    id: root

    readonly property var screenStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lockscreen") : ({})
    readonly property var ringStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("lock-ring") : ({})

    readonly property string displayName: (typeof userModel !== "undefined" && userModel && userModel.realName.length)
        ? userModel.realName : lockController.user
    readonly property string avatarSource: (typeof userModel !== "undefined" && userModel && userModel.iconPath)
        ? userModel.iconPath : ""

    function styleColor(style, name, fallback) {
        return style && style[name] ? cssTheme.parseColor(style[name]) : fallback
    }

    readonly property bool busy: lockController.busy
    readonly property bool hasError: lockController.error.length > 0
    property bool success: false

    readonly property color idleColor: root.styleColor(root.ringStyle, "color", "#7aa2f7")
    readonly property color busyColor: root.styleColor(root.ringStyle, "busy-color", "#e0af68")
    readonly property color errorColor: root.styleColor(root.ringStyle, "error-color", "#e0574b")
    readonly property color okColor: root.styleColor(root.ringStyle, "success-color", "#9ece6a")

    // Solid backdrop (theme #lockscreen background-color, else near-black like i3lock).
    QBar.CssFill {
        anchors.fill: parent
        style: root.screenStyle
        defaultColor: "#161616"
    }

    Item {
        id: ring
        anchors.centerIn: parent
        width: 150
        height: 150

        // A brief scale bump gives each keystroke tactile feedback (i3lock's per-key arc).
        scale: 1.0
        Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutQuad } }
        // Horizontal shake on a failed attempt.
        property real shake: 0
        x: shake

        Rectangle {
            id: ringCircle
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 8
            border.color: root.success ? root.okColor
                        : root.hasError ? root.errorColor
                        : root.busy ? root.busyColor
                        : root.idleColor
            opacity: 0.92
            Behavior on border.color { ColorAnimation { duration: 180 } }
        }

        // Orbiting dot while authenticating.
        Item {
            anchors.fill: parent
            opacity: root.busy ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            RotationAnimator on rotation {
                running: root.busy
                loops: Animation.Infinite
                from: 0; to: 360; duration: 1100
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: root.busyColor
                x: parent.width / 2 - width / 2
                y: -3
            }
        }

        // Avatar inside the ring; falls back to a state-tinted disc + monogram.
        Item {
            anchors.centerIn: parent
            width: parent.width - 30
            height: width

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: ringCircle.border.color
                opacity: 0.12
                Text {
                    anchors.centerIn: parent
                    text: root.displayName.length ? root.displayName[0].toUpperCase() : "?"
                    color: root.styleColor(root.ringStyle, "text-color", "#c8d0e0")
                    font.pixelSize: 34
                    opacity: avatarImg.status === Image.Ready ? 0 : 0.9
                }
            }
            Image {
                id: avatarImg
                anchors.fill: parent
                source: root.avatarSource
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 220
                sourceSize.height: 220
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
    }

    // Name, live prompt/hint and Caps/Num Lock, stacked under the ring.
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: ring.bottom
        anchors.topMargin: 22
        spacing: 8

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.styleColor(root.ringStyle, "text-color", "#c8d0e0")
            opacity: 0.9
            font.pixelSize: 15
            text: root.displayName
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            color: root.hasError ? root.errorColor : root.styleColor(root.ringStyle, "text-color", "#c8d0e0")
            opacity: 0.8
            font.pixelSize: 13
            text: {
                if (root.hasError)
                    return lockController.error
                var parts = []
                if (lockController.fingerprintActive) parts.push("Touch the fingerprint reader")
                if (lockController.faceActive) parts.push("Look at the camera")
                return parts.length ? parts.join("   ·   ") : lockController.message
            }
        }

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

    // Invisible keystroke sink — no password box is drawn, matching i3lock.
    TextInput {
        id: passwordInput
        width: 0
        height: 0
        opacity: 0
        focus: true
        echoMode: TextInput.Password
        onTextChanged: {
            // Pulse the ring on any edit (type or delete).
            ring.scale = 1.09
            pulseReset.restart()
        }
        onAccepted: {
            var password = text
            text = ""
            lockController.submitPassword(password)
        }
    }

    Timer {
        id: pulseReset
        interval: 90
        onTriggered: ring.scale = 1.0
    }

    // Shake on a new error.
    Connections {
        target: lockController
        function onErrorChanged() {
            if (lockController.error.length > 0)
                shakeAnim.restart()
        }
        function onUnlocked() {
            root.success = true
        }
    }

    SequentialAnimation {
        id: shakeAnim
        NumberAnimation { target: ring; property: "shake"; to: -14; duration: 50 }
        NumberAnimation { target: ring; property: "shake"; to: 14; duration: 90 }
        NumberAnimation { target: ring; property: "shake"; to: -8; duration: 70 }
        NumberAnimation { target: ring; property: "shake"; to: 0; duration: 60 }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            lockController.cancel()
            event.accepted = true
        }
    }

    Component.onCompleted: passwordInput.forceActiveFocus()
}
