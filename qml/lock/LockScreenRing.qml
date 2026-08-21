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
    readonly property string avatarSource: (typeof lockHideAvatar !== "undefined" && lockHideAvatar) ? ""
        : (typeof userModel !== "undefined" && userModel && userModel.iconPath)
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
    // Per-keystroke arc highlight (i3lock): a BRIGHT segment for a typed char (must
    // contrast the ring itself, which is idleColor) and a distinct amber for a delete.
    // Both opaque — the fade is done via Canvas globalAlpha.
    readonly property color keypressColor: root.styleColor(root.ringStyle, "keypress-color", Qt.lighter(root.idleColor, 1.6))
    readonly property color keypressClearColor: root.styleColor(root.ringStyle, "keypress-clear-color", root.busyColor)
    readonly property color keypressEmptyColor: root.styleColor(root.ringStyle, "keypress-empty-color", "#8890a0")

    // Transient "empty" notice: shown briefly when Backspace clears the buffer (or is
    // pressed on an already-empty buffer), like i3lock's cleared-ring state.
    property bool emptyActive: false
    function flashEmpty() { emptyActive = true; emptyTimer.restart() }

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

        // i3lock per-keystroke flourish: each key lights a randomly-placed arc segment
        // ("pizza slice") on the ring that fades out. Drawn with Canvas (Context2D/QPainter)
        // — NOT QtQuick.Shapes, whose CurveRenderer would paint the fading (alpha) stroke
        // black. Per-segment alpha via ctx.globalAlpha; several can overlap while typing fast.
        Canvas {
            id: arcCanvas
            anchors.fill: parent
            antialiasing: true
            property var segments: []   // each: { start, sweep, alpha, color }

            // Escape wipe: one arc that grows from the top until it fills the whole
            // ring, then fades — "everything you typed was swept away".
            property real wipeSweep: 0
            property real wipeAlpha: 0
            property color wipeColor: "transparent"
            onWipeSweepChanged: requestPaint()
            onWipeAlphaChanged: requestPaint()

            function startWipe(color) {
                wipeColor = color
                wipeAnim.restart()
            }

            SequentialAnimation {
                id: wipeAnim
                ScriptAction { script: arcCanvas.wipeAlpha = 1 }
                NumberAnimation {
                    target: arcCanvas; property: "wipeSweep"
                    from: 0; to: 2 * Math.PI
                    duration: 320; easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: arcCanvas; property: "wipeAlpha"
                    from: 1; to: 0; duration: 260
                }
                ScriptAction { script: arcCanvas.wipeSweep = 0 }
            }

            function addSegment(color) {
                var start = Math.random() * 2 * Math.PI
                var sweep = (35 + Math.random() * 20) * Math.PI / 180
                var segs = segments
                segs.push({ start: start, sweep: sweep, alpha: 1.0, color: color })
                if (segs.length > 6)
                    segs.shift()
                segments = segs
                requestPaint()
                fadeTimer.start()
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width / 2
                var cy = height / 2
                // Centre the stroke on the ring border (match ringCircle line width/radius).
                var r = width / 2 - ringCircle.border.width / 2
                ctx.lineWidth = ringCircle.border.width
                ctx.lineCap = "round"
                for (var i = 0; i < segments.length; ++i) {
                    var s = segments[i]
                    if (s.alpha <= 0)
                        continue
                    ctx.globalAlpha = s.alpha
                    ctx.strokeStyle = s.color
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, s.start, s.start + s.sweep, false)
                    ctx.stroke()
                }
                if (wipeAlpha > 0 && wipeSweep > 0) {
                    ctx.globalAlpha = wipeAlpha
                    ctx.strokeStyle = wipeColor
                    ctx.beginPath()
                    // From 12 o'clock, clockwise.
                    ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + wipeSweep, false)
                    ctx.stroke()
                }
            }

            Timer {
                id: fadeTimer
                interval: 16
                repeat: true
                running: false
                onTriggered: {
                    var segs = arcCanvas.segments
                    var alive = false
                    for (var i = 0; i < segs.length; ++i) {
                        segs[i].alpha -= 0.035   // ~450ms fade at 60fps
                        if (segs[i].alpha > 0)
                            alive = true
                    }
                    arcCanvas.segments = segs
                    arcCanvas.requestPaint()
                    if (!alive) {
                        arcCanvas.segments = []
                        running = false
                    }
                }
            }
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
                if (root.emptyActive)
                    return qsTr("empty")
                var parts = []
                if (lockController.fingerprintActive) parts.push(qsTr("Touch the fingerprint reader"))
                if (lockController.faceActive) parts.push(qsTr("Look at the camera"))
                return parts.length ? parts.join("   ·   ") : lockController.message
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18
            Text {
                text: keyLocks.active ? qsTr("CAPS LOCK ON") : qsTr("Caps Lock")
                color: keyLocks.active ? "#f7768e" : "#5b6272"
                font.bold: keyLocks.active
                font.pixelSize: 12
            }
            Text {
                text: keyLocks.numLockActive ? qsTr("Num Lock On") : qsTr("Num Lock Off")
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
        // Drive the arc on the KEY EVENT, not onTextChanged: Backspace on an already-empty
        // buffer doesn't change the text (so onTextChanged never fires) — but i3lock still
        // flashes it. Keys.onPressed sees every physical key.
        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                // i3lock: Escape wipes everything typed so far. On an already-empty
                // buffer it falls through to cancel() (quits the --demo harness).
                if (passwordInput.text.length > 0) {
                    passwordInput.text = ""
                    lockController.clearError()
                    ring.scale = 1.09
                    pulseReset.restart()
                    arcCanvas.startWipe(root.keypressClearColor)
                    root.flashEmpty()
                } else {
                    lockController.cancel()
                }
                event.accepted = true
                return
            }

            // Only keys that actually edit the buffer get feedback: a delete, or a key
            // producing printable text (event.text is empty for arrows/F-keys/modifiers
            // and a control char for Return/Tab/Ctrl-chords — none of those are input).
            var isDelete = event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete
            var isText = !isDelete && event.text.length > 0
                && event.text.charCodeAt(0) >= 0x20 && event.text !== "\x7f"
            if (!isText && !isDelete)
                return
            // Backspace on an already-empty buffer edits nothing — stay quiet.
            if (isDelete && passwordInput.text.length === 0)
                return

            // Editing again after a failed attempt starts a fresh one — drop the stale red.
            if (root.hasError)
                lockController.clearError()

            // At key-press time the edit hasn't happened yet: 1 char left + delete → empty.
            var goesEmpty = isDelete && passwordInput.text.length === 1

            ring.scale = 1.09
            pulseReset.restart()
            if (goesEmpty) {
                arcCanvas.addSegment(root.keypressEmptyColor)
                root.flashEmpty()
            } else {
                arcCanvas.addSegment(isDelete ? root.keypressClearColor : root.keypressColor)
            }
            // Not accepted: let the TextInput edit the buffer / fire onAccepted on Return.
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

    Timer {
        id: emptyTimer
        interval: 700
        onTriggered: root.emptyActive = false
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

    Component.onCompleted: passwordInput.forceActiveFocus()
}
