import QtQuick
import "qrc:/qbar" as QBar

// One toast. Fully CSS-themed via the part/state convention:
//
//   #notification                — card container: background(-color/gradient), border,
//                                  border-radius, box-shadow, padding, transition,
//                                  and the ENTRY `animation: <keyframes> <ms> <easing>`
//                                  (:low/:normal/:critical urgency states, :hover)
//   #notification.app            — app-name/timestamp row (color, font-size)
//   #notification.summary        — title (color, font-size, font-weight)
//   #notification.body           — body (color, font-size, link-color for <a href>)
//   #notification.icon           — icon box (width = icon size, border-radius)
//   #notification.close          — close ✕ (color; :hover state)
//   #notification.action         — action buttons (background-color, color, border…, :hover)
//   #notification.progress       — timeout countdown bar (background-color track, color fill, height)
//   #notification.value          — the `value` hint gauge (same properties as .progress)
//
// Entry `@keyframes` may animate `opacity` and `transform` (translateX/translateY px,
// scale); absent CSS falls back to a slide-from-the-corner + fade.
Item {
    id: card

    property bool visibleInStack: true
    property bool slideFromLeft: false

    // Safe snapshots of the model roles. The remove transition keeps this delegate
    // alive ~200ms AFTER its row left the model; the raw context roles turn undefined
    // then, and any `role.length`/`role ? …` binding would throw TypeErrors mid-exit
    // (breaking the exit visuals). Every read below goes through these instead.
    readonly property int nId: notifId !== undefined ? notifId : 0
    readonly property string nApp: appName !== undefined ? appName : ""
    readonly property string nAppIcon: appIcon !== undefined ? appIcon : ""
    readonly property string nSummary: summary !== undefined ? summary : ""
    readonly property string nBody: body !== undefined ? body : ""
    readonly property int nUrgency: urgency !== undefined ? urgency : 1
    readonly property var nActions: actions !== undefined ? actions : []
    readonly property bool nHasDefaultAction: hasDefaultAction !== undefined ? hasDefaultAction : false
    readonly property string nImage: imageSource !== undefined ? imageSource : ""
    readonly property real nValue: value !== undefined ? value : -1
    readonly property int nExpireMs: expireMs !== undefined ? expireMs : 0
    readonly property var nTimestamp: timestamp

    readonly property string urgencyClass: nUrgency === 2 ? "critical" : (nUrgency === 0 ? "low" : "normal")
    // Card-level hover MUST be a HoverHandler, not the MouseArea's containsMouse: the
    // ✕ and action buttons carry their own hover-enabled MouseAreas, and in Qt 6 those
    // steal hover from an underlying MouseArea — the card would "unhover" (✕ vanishing,
    // expansion collapsing) the moment the pointer reaches a button. Handlers see hover
    // in parallel with children, so `hovered` holds anywhere over the card.
    readonly property bool hovered: cardHover.hovered
    readonly property var stateClasses: card.hovered
        ? [card.urgencyClass, "hover"] : [card.urgencyClass]

    readonly property real pad: bg.style["padding"] ? cssTheme.parseLength(bg.style["padding"], 12) : 12
    readonly property real iconSize: iconStyle["width"] ? cssTheme.parseLength(iconStyle["width"], 42) : 42
    readonly property var iconStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolvePart("notification", "icon") : ({})
    readonly property var progressStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolvePart("notification", "progress") : ({})
    readonly property var valueStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolvePart("notification", "value") : ({})
    readonly property bool showProgress: nExpireMs > 0 && progressStyle["background-color"] !== undefined

    visible: visibleInStack
    height: visibleInStack ? content.implicitHeight + 2 * pad + (showProgress ? progressBar.height : 0) : 0
    clip: false

    // Hovering a card with elided text grows it so the whole message is readable
    // (the line caps below are lifted while hovered). The grow follows the card's
    // CSS `transition` duration; the input region tracks it via stackHeight.
    readonly property bool expanded: card.hovered
    Behavior on height {
        NumberAnimation {
            duration: bg.transitionMs > 0 ? bg.transitionMs : 160
            easing.type: bg.transitionEasingType
        }
    }

    // ---- Entry/exit animation: CSS `animation` shorthand + @keyframes (opacity/transform) ----
    // Entry: `#notification { animation: <keyframes> <ms> <easing> }`, run on creation.
    // Exit:  `#notification:exit { animation: ... }` (per-urgency `:critical:exit` works);
    //        NotificationSurface's remove transition drives `exitProgress` 0→1.
    // Both fall back to a fade + slide toward the corner's screen edge.
    readonly property var entrySpec: (cssTheme && cssTheme.loaded && bg.style["animation"])
        ? cssTheme.parseAnimation(bg.style["animation"]) : ({})
    readonly property var entryFrames: entrySpec.name ? cssTheme.keyframes(entrySpec.name) : []
    readonly property int entryMs: entrySpec.duration !== undefined ? entrySpec.duration : 280
    readonly property int entryEasing: entrySpec.easing !== undefined ? entrySpec.easing : Easing.OutCubic

    readonly property var exitStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("notification", [card.urgencyClass, "exit"]) : ({})
    readonly property var exitSpec: exitStyle["animation"]
        ? cssTheme.parseAnimation(exitStyle["animation"]) : ({})
    readonly property var exitFrames: exitSpec.name ? cssTheme.keyframes(exitSpec.name) : []

    property real entryProgress: 1.0
    property real exitProgress: 0.0
    property real poseOpacity: 1.0
    property real poseTx: 0
    property real poseTy: 0
    property real poseScale: 1.0

    opacity: poseOpacity
    transform: [
        Scale {
            origin.x: card.width / 2
            origin.y: card.height / 2
            xScale: card.poseScale
            yScale: card.poseScale
        },
        Translate { x: card.poseTx; y: card.poseTy }
    ]

    function frameValues(f) {
        var t = f.properties["transform"] ? cssTheme.parseTransform(f.properties["transform"]) : {}
        return {
            opacity: f.properties["opacity"] !== undefined ? parseFloat(f.properties["opacity"]) : 1,
            tx: t.translateX !== undefined ? t.translateX : 0,
            ty: t.translateY !== undefined ? t.translateY : 0,
            scale: t.scale !== undefined ? t.scale : 1
        }
    }

    // Interpolate the @keyframes at `p` (0..1): opacity + parsed transform per frame.
    function interpolateFrames(ks, p) {
        var a = frameValues(ks[0])
        if (p <= ks[0].offset)
            return a
        for (var i = 0; i < ks.length - 1; ++i) {
            if (p >= ks[i].offset && p <= ks[i + 1].offset) {
                a = frameValues(ks[i])
                var b = frameValues(ks[i + 1])
                var span = ks[i + 1].offset - ks[i].offset
                var t = span > 0 ? (p - ks[i].offset) / span : 1
                return {
                    opacity: a.opacity + (b.opacity - a.opacity) * t,
                    tx: a.tx + (b.tx - a.tx) * t,
                    ty: a.ty + (b.ty - a.ty) * t,
                    scale: a.scale + (b.scale - a.scale) * t
                }
            }
        }
        return frameValues(ks[ks.length - 1])
    }

    function entryValuesAt(p) {
        var ks = card.entryFrames
        if (!ks || ks.length === 0) {
            // Fallback: slide in from the corner's screen edge + fade.
            var from = card.slideFromLeft ? -card.width : card.width
            return { opacity: p, tx: from * (1 - p), ty: 0, scale: 1 }
        }
        return interpolateFrames(ks, p)
    }

    function exitValuesAt(p) {
        var ks = card.exitFrames
        if (!ks || ks.length === 0) {
            // Fallback: fade + slide back out over the corner's screen edge.
            var to = card.slideFromLeft ? -card.width : card.width
            return { opacity: 1 - p, tx: to * p, ty: 0, scale: 1 }
        }
        return interpolateFrames(ks, p)
    }

    function applyPose(v) {
        poseOpacity = v.opacity
        poseTx = v.tx
        poseTy = v.ty
        poseScale = v.scale
    }

    onEntryProgressChanged: applyPose(entryValuesAt(entryProgress))
    onExitProgressChanged: {
        if (exitProgress > 0) {
            // The exit owns the pose from its first tick. The entry animation can
            // still be mid-flight here — render-driven animations advance in FRAME
            // time, so on a callback-starved window a 280ms entry may sit unfinished
            // for minutes. Left running, the two animations alternate writes on the
            // shared pose each tick and the card visibly snaps back toward its
            // normal (entry) pose mid-dismiss.
            if (entryAnimation.running)
                entryAnimation.stop()
            applyPose(exitValuesAt(exitProgress))
        }
    }

    NumberAnimation {
        id: entryAnimation
        target: card
        property: "entryProgress"
        from: 0.0
        to: 1.0
        duration: Math.max(1, card.entryMs)
        easing.type: card.entryEasing
    }

    Component.onCompleted: {
        console.info("[notif] card created id=" + nId + " summary=" + nSummary)
        entryProgress = 0.0
        applyPose(entryValuesAt(0))
        entryAnimation.start()
    }
    Component.onDestruction: console.info("[notif] card destroyed id=" + nId
        + " exitProgress=" + exitProgress.toFixed(2))

    // ---- Card chrome ----
    // `#notification { emboss: <0..1> }` swaps the flat CssRect fill for the
    // PopupEmboss shader — a rounded slab with a soft gradient bevel (the same
    // shader the popups compile). The base colour keeps its CSS alpha, so a
    // translucent embossed card + a compositor blur rule = frosted glass with
    // relief. Optional: emboss-highlight / emboss-shadow (colors), emboss-edge (px).
    readonly property bool useEmboss: typeof popupEmbossShaderAvailable !== "undefined"
        && popupEmbossShaderAvailable && bg.style["emboss"] !== undefined

    QBar.CssRect {
        id: bg
        anchors.fill: parent
        cssId: "notification"
        cssClass: card.stateClasses
        radius: 10
        defaultColor: "#2b3140"
        defaultBorderColor: Qt.rgba(1, 1, 1, 0.14)
        defaultBorderWidth: 1
        // With emboss on, the shader is the surface; the CssRect stays as the
        // style carrier (loadCss target) and box-shadow parser, but must not
        // double-paint the fill under the translucent shader.
        opacity: card.useEmboss ? 0 : 1
    }

    Item {
        anchors.fill: parent
        visible: card.useEmboss
        layer.enabled: card.useEmboss && bg.hasOutsetShadow
        layer.effect: QBar.CssDropShadow { shadow: bg.outsetShadow }

        ShaderEffect {
            anchors.fill: parent
            property color baseColor: bg.solidColor
            property color highlightColor: bg.style["emboss-highlight"] !== undefined
                ? cssTheme.parseColor(bg.style["emboss-highlight"]) : Qt.rgba(1, 1, 1, 0.92)
            property color shadowColor: bg.style["emboss-shadow"] !== undefined
                ? cssTheme.parseColor(bg.style["emboss-shadow"]) : Qt.rgba(0, 0, 0, 0.85)
            property real edgeSize: (bg.style["emboss-edge"] !== undefined
                ? cssTheme.parseLength(bg.style["emboss-edge"], 10) : 10)
                / Math.max(1, Math.min(width, height))
            property real cornerRadius: bg.clampedRadius(0) / Math.max(1, Math.min(width, height))
            property real padding: 0
            property real intensity: Math.max(0, Math.min(1, parseFloat(bg.style["emboss"])))
            fragmentShader: "qrc:/qbar/shaders/PopupEmboss.frag.qsb"
        }
    }

    HoverHandler {
        id: cardHover
        onHoveredChanged: {
            notificationServer.setHovered(card.nId, hovered)
            // Freeze the countdown visual with the (server-side) expiry timer.
            if (countdown.running)
                countdown.paused = hovered
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: function(ev) {
            // Right or middle click always dismisses, whatever actions the card has.
            if (ev.button === Qt.MiddleButton || ev.button === Qt.RightButton) {
                notificationServer.dismiss(card.nId)
                return
            }
            if (card.nHasDefaultAction) {
                notificationServer.invokeAction(card.nId, "default")
            } else if (card.nActions.length === 0) {
                notificationServer.dismiss(card.nId)
            }
        }
    }

    Row {
        id: content
        x: card.pad
        y: card.pad
        width: parent.width - 2 * card.pad
        spacing: card.pad

        Image {
            id: icon
            width: card.iconSize
            height: card.iconSize
            visible: source.toString().length > 0
            source: card.nImage.length > 0 ? card.nImage : card.nAppIcon
            sourceSize.width: card.iconSize * 2
            sourceSize.height: card.iconSize * 2
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        Column {
            width: parent.width - (icon.visible ? icon.width + content.spacing : 0)
            spacing: 4

            QBar.CssText {
                width: parent.width - closeButton.width
                cssId: "notification"
                cssPart: "app"
                cssClass: [card.urgencyClass]
                visible: card.nApp.length > 0
                opacity: 0.75
                text: card.nApp + "  ·  " + Qt.formatTime(card.nTimestamp, "HH:mm")
                elide: Text.ElideRight
                defaultColor: theme.foreground
            }

            QBar.CssText {
                width: parent.width - (card.nApp.length > 0 ? 0 : closeButton.width)
                cssId: "notification"
                cssPart: "summary"
                cssClass: [card.urgencyClass]
                visible: card.nSummary.length > 0
                text: card.nSummary
                font.bold: true
                wrapMode: Text.Wrap
                maximumLineCount: card.expanded ? 20 : 2
                elide: Text.ElideRight
            }

            QBar.CssText {
                width: parent.width
                cssId: "notification"
                cssPart: "body"
                cssClass: [card.urgencyClass]
                visible: card.nBody.length > 0
                text: card.nBody
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                maximumLineCount: card.expanded ? 100 : 6
                elide: Text.ElideRight
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }

            // `value` hint gauge (volume/brightness OSD style).
            Item {
                width: parent.width
                height: card.nValue >= 0 ? 6 : 0
                visible: card.nValue >= 0
                QBar.CssRect {
                    anchors.fill: parent
                    cssId: "notification"
                    cssPart: "value"
                    cssClass: [card.urgencyClass]
                    radius: 3
                    defaultColor: Qt.rgba(1, 1, 1, 0.12)
                }
                QBar.CssRect {
                    width: parent.width * card.nValue / 100
                    height: parent.height
                    style: ({ "background-color": card.valueStyle["color"] !== undefined
                                  ? card.valueStyle["color"] : theme.accent })
                    radius: 3
                }
            }

            // Action buttons.
            Flow {
                width: parent.width
                spacing: 6
                visible: card.nActions.length > 0

                Repeater {
                    model: card.nActions
                    delegate: Item {
                        width: actionLabel.implicitWidth + 20
                        height: actionLabel.implicitHeight + 10
                        QBar.CssRect {
                            anchors.fill: parent
                            cssId: "notification"
                            cssPart: "action"
                            cssClass: actionMouse.containsMouse
                                ? [card.urgencyClass, "hover"] : [card.urgencyClass]
                            radius: 6
                            defaultColor: Qt.rgba(1, 1, 1, 0.10)
                        }
                        QBar.CssText {
                            id: actionLabel
                            anchors.centerIn: parent
                            cssId: "notification"
                            cssPart: "action"
                            cssClass: [card.urgencyClass]
                            text: modelData.label
                        }
                        MouseArea {
                            id: actionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: notificationServer.invokeAction(card.nId, modelData.key)
                        }
                    }
                }
            }
        }
    }

    // Close ✕ — shown while hovering the card (critical cards always show it: they
    // have no timeout, the ✕ is how they leave).
    Item {
        id: closeButton
        width: 22
        height: 22
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        anchors.rightMargin: 6
        opacity: card.hovered || card.nUrgency === 2 ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        QBar.CssText {
            anchors.centerIn: parent
            cssId: "notification"
            cssPart: "close"
            cssClass: closeMouse.containsMouse
                ? [card.urgencyClass, "hover"] : [card.urgencyClass]
            text: "✕"
            defaultColor: theme.foreground
        }
        MouseArea {
            id: closeMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: notificationServer.dismiss(card.nId)
        }
    }

    // Timeout countdown bar along the bottom edge — opt-in: themed via
    // `#notification.progress { background-color: ... }`. Pauses with the expiry
    // timer while hovered (the server pauses; we pause the visual).
    Item {
        id: progressBar
        visible: card.showProgress
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: bg.borderWidth
        anchors.rightMargin: bg.borderWidth
        anchors.bottomMargin: bg.borderWidth
        height: card.progressStyle["height"] ? cssTheme.parseLength(card.progressStyle["height"], 3) : 3

        property real remaining: 1.0

        QBar.CssRect {
            anchors.fill: parent
            cssId: "notification"
            cssPart: "progress"
            cssClass: [card.urgencyClass]
        }
        Rectangle {
            width: parent.width * progressBar.remaining
            height: parent.height
            color: card.progressStyle["color"] !== undefined
                ? cssTheme.parseColor(card.progressStyle["color"]) : cssTheme.parseColor(theme.accent)
        }

        NumberAnimation {
            id: countdown
            target: progressBar
            property: "remaining"
            from: 1.0
            to: 0.0
            duration: Math.max(1, card.nExpireMs)
            running: card.showProgress
        }
    }

    // A replaces_id / stack-tag update restarts the timeout server-side; mirror it
    // visually (role bindings alone don't restart a running animation). The timestamp
    // watcher covers replaces where expireMs resolves to the same value.
    readonly property int liveExpireMs: nExpireMs
    onLiveExpireMsChanged: restartCountdown()
    readonly property var liveStamp: nTimestamp
    onLiveStampChanged: restartCountdown()

    function restartCountdown() {
        if (card.showProgress) {
            progressBar.remaining = 1.0
            countdown.restart()
        }
    }
}
