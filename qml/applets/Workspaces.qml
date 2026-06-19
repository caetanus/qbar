import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: preferredWidth

    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("workspaces") : ({})
    readonly property real containerPaddingLeft: paddingSide(cssStyle, "left", 0)
    readonly property real containerPaddingRight: paddingSide(cssStyle, "right", 0)
    readonly property real containerPaddingTop: paddingSide(cssStyle, "top", 0)
    readonly property real containerPaddingBottom: paddingSide(cssStyle, "bottom", 0)
    readonly property real containerRadius: cssPixels(cssStyle, "border-radius", 0)

    property int preferredWidth: Math.ceil(contentRow.childrenRect.width + containerPaddingLeft + containerPaddingRight)

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

    function cssPixels(style, key, fallback) {
        if (!style || style[key] === undefined || style[key] === null || style[key] === "") {
            return fallback
        }
        var n = parseFloat(style[key])
        return isNaN(n) ? fallback : n
    }

    function paddingSide(style, side, fallback) {
        if (!style) {
            return fallback
        }
        var explicit = style["padding-" + side]
        if (explicit) {
            return parseFloat(explicit)
        }
        var shorthand = style["padding"]
        if (!shorthand) {
            return fallback
        }
        var parts = String(shorthand).trim().split(/\s+/)
        if (parts.length === 1) return parseFloat(parts[0])
        if (parts.length === 2) return (side === "top" || side === "bottom") ? parseFloat(parts[0]) : parseFloat(parts[1])
        if (parts.length === 3) {
            if (side === "top") return parseFloat(parts[0])
            if (side === "bottom") return parseFloat(parts[2])
            return parseFloat(parts[1])
        }
        if (side === "top") return parseFloat(parts[0])
        if (side === "right") return parseFloat(parts[1])
        if (side === "bottom") return parseFloat(parts[2])
        return parseFloat(parts[3])
    }

    function paddingX(style) {
        if (style && style["padding-left"]) {
            return root.cssPixels(style, "padding-left", 4)
        }
        if (style && style["padding"]) {
            var parts = String(style["padding"]).trim().split(/\s+/)
            if (parts.length === 1) return parseFloat(parts[0])
            if (parts.length === 2 || parts.length === 3) return parseFloat(parts[1])
            return parseFloat(parts[3])
        }
        return 4
    }

    function buttonStyle(classes) {
        return cssTheme && cssTheme.loaded ? cssTheme.resolveWith("workspaces", "button", classes) : ({})
    }

    // A `::before`/`::after` decorative overlay of a workspace button, e.g.
    // `#workspaces button.urgent::before { background: rgba(...) }`.
    function buttonOverlay(classes, pseudo) {
        return cssTheme && cssTheme.loaded ? cssTheme.resolveWith("workspaces", "button", classes, pseudo) : ({})
    }

    // Resolve a fill colour from a style's `background-color` (preferred) or
    // `background`; returns "" when neither is set.
    function fillColorOf(style) {
        if (!style) return ""
        return style["background-color"] || style["background"] || ""
    }

    // theme.* colours are HexArgb STRINGS, so `theme.accent.r` is undefined and
    // Qt.rgba(undefined,...) yields opaque/translucent black. Parse first, then tint.
    function alphaColor(colorStr, a) {
        var c = cssTheme.parseColor(colorStr)
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    function cssDuration(style, key, fallback) {
        return cssTheme && cssTheme.loaded ? cssTheme.parseDuration(style ? (style[key] || "") : "", fallback) : fallback
    }

    function cssEasing(style, key, fallback) {
        return cssTheme && cssTheme.loaded ? cssTheme.parseEasing(style ? (style[key] || "") : "", fallback) : fallback
    }

    // Resolve the standard CSS `transition` for a button state: the shorthand
    // (`transition: <prop> <dur> <timing-function>`) is preferred, else the
    // longhands (`transition-duration` + `transition-timing-function`). Returns
    // { ms, easing }; ms 0 means no animation — the bar only animates when the
    // theme asks for it. `transition` lives on the base `#workspaces button`, so
    // resolveWith merges it into every state and the fade stays consistent.
    function transitionOf(style) {
        if (!style || !(cssTheme && cssTheme.loaded))
            return { ms: 0, easing: Easing.InOutQuad }
        if (style["transition"]) {
            var t = cssTheme.parseTransition(style["transition"])
            return { ms: t.duration || 0, easing: t.easing }
        }
        if (style["transition-duration"]) {
            return {
                ms: cssTheme.parseDuration(style["transition-duration"], 0),
                easing: cssTheme.parseEasing(style["transition-timing-function"] || "", Easing.InOutQuad)
            }
        }
        return { ms: 0, easing: Easing.InOutQuad }
    }

    QBar.CssFill {
        anchors.fill: parent
        style: root.cssStyle
        radius: root.containerRadius
        defaultColor: "transparent"
    }

    // Rounded clip mask for the tile row (see contentRow.layer below). Hidden;
    // rendered to a layer texture and sampled by the MultiEffect as the clip shape.
    Item {
        id: tileMask
        anchors.fill: contentRow
        visible: false
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            radius: root.containerRadius
            antialiasing: true
            color: "black"
        }
    }

    Row {
        id: contentRow
        spacing: 0
        anchors.left: parent.left
        anchors.leftMargin: root.containerPaddingLeft
        anchors.rightMargin: root.containerPaddingRight
        anchors.top: parent.top
        anchors.topMargin: root.containerPaddingTop
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.containerPaddingBottom

        // QML `clip` is rectangular and ignores `radius`, so the square-cornered
        // tiles spill past the rounded #workspaces corners. Render the tile row to a
        // layer and mask it to a rounded rectangle matching the container radius.
        layer.enabled: root.containerRadius > 0
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: tileMask
            maskThresholdMin: 0.5
        }

        Repeater {
            id: ipcRepeater
            model: workspaceModel && !workspaceModel.empty ? workspaceModel : 0

            Rectangle {
                id: workspaceTile
                height: contentRow.height
                radius: 0
                clip: true
                property real hoverReveal: 0.0
                property real hoverOpacity: 0.0
                property bool attention: urgent
                property real activeProgress: focused ? 1.0 : 0.0

                // #workspaces button[.focused/.visible/.urgent] from the CSS theme
                readonly property var stateClasses: urgent ? ["urgent"] : focused ? ["focused"] : visible ? ["visible"] : []
                readonly property var cssStyle: root.buttonStyle(stateClasses)
                // #workspaces button:hover (combined with the current state) — drives
                // the animated hover highlight overlay below.
                readonly property var hoverStyle: root.buttonStyle(stateClasses.concat(["hover"]))
                readonly property real tilePaddingX: root.paddingX(cssStyle)
                readonly property real tileMinWidth: root.cssPixels(cssStyle, "min-width", 0)
                readonly property real tileRadius: root.cssPixels(cssStyle, "border-radius", 0)
                // Active-fill overlay: #workspaces button.focused::after { background: rgba(...) }.
                // Blink overlay: #workspaces button.urgent::before { background: rgba(...) }.
                // Opacity is the alpha of the colour; the QML animates opacity over it.
                readonly property var activeFillStyle: root.buttonOverlay(["focused"], "after")
                readonly property var blinkStyle: root.buttonOverlay(["urgent"], "before")
                // Standard CSS `transition` (shorthand or longhands) driving the
                // background fade between states. The CssFill below animates the
                // fill colour AND opacity with this duration/easing, synced.
                readonly property var tileTransition: root.transitionOf(cssStyle)
                readonly property int activeDuration: root.cssDuration(cssStyle, "active-duration", 240)
                readonly property int activeEasing: root.cssEasing(cssStyle, "active-easing", Easing.InOutCubic)
                // Urgent-blink via standard CSS `@keyframes` + `animation` on
                // `#workspaces button.urgent::before` (no `animation-*` quirks).
                readonly property var blinkAnim: (cssTheme && cssTheme.loaded)
                    ? cssTheme.parseAnimation(blinkStyle["animation"] || "") : ({})
                readonly property var blinkFrames: (cssTheme && cssTheme.loaded && blinkAnim.name)
                    ? cssTheme.keyframes(blinkAnim.name) : []
                readonly property bool urgentBlinkEnabled: attention
                    && root.fillColorOf(blinkStyle).length > 0 && blinkFrames.length > 0
                readonly property color tileBg: cssStyle["background-color"]
                    ? cssTheme.parseColor(cssStyle["background-color"])
                    : (workspaceTile.attention ? "#ff5555" : visible ? "#526171" : "#273847")
                readonly property color tileFg: cssStyle["color"]
                    ? cssTheme.parseColor(cssStyle["color"])
                    : (workspaceTile.attention ? "#ffffff" : "#eef2f7")

                width: Math.max(label.implicitWidth + tilePaddingX * 2, tileMinWidth)
                color: "transparent"

                QBar.CssFill {
                    anchors.fill: parent
                    style: workspaceTile.cssStyle
                    radius: workspaceTile.tileRadius
                    defaultColor: workspaceTile.tileBg
                    transitionMs: workspaceTile.tileTransition.ms
                    transitionEasingType: workspaceTile.tileTransition.easing
                }

                // Animated hover highlight. The colour comes from the CSS
                // `#workspaces button:hover { background-color }` (can be a light
                // tint to brighten or a dark tint to dim); opacity animates in/out.
                Rectangle {
                    id: hoverFill
                    anchors.fill: parent
                    radius: workspaceTile.tileRadius
                    color: workspaceTile.hoverStyle["background-color"]
                        ? cssTheme.parseColor(workspaceTile.hoverStyle["background-color"])
                        : "transparent"
                    opacity: workspaceTile.hoverOpacity
                    visible: opacity > 0.0 && color.a > 0.0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.cssDuration(workspaceTile.cssStyle, "hover-duration", 240)
                            easing.type: root.cssEasing(workspaceTile.cssStyle, "hover-easing", Easing.OutCubic)
                        }
                    }
                }

                Behavior on activeProgress {
                    NumberAnimation {
                        duration: workspaceTile.activeDuration
                        easing.type: workspaceTile.activeEasing
                    }
                }

                Rectangle {
                    id: urgentBlinkFill
                    anchors.fill: parent
                    radius: workspaceTile.tileRadius
                    color: {
                        var bg = root.fillColorOf(workspaceTile.blinkStyle)
                        return bg.length > 0 ? cssTheme.parseColor(bg) : "#00000000"
                    }
                    opacity: 0.0
                    visible: workspaceTile.urgentBlinkEnabled

                    // The ::before overlay's opacity is driven through the theme's
                    // `@keyframes` by the `animation` params (name/duration/easing/iters).
                    QBar.CssKeyframes {
                        target: urgentBlinkFill
                        animatedProperty: "opacity"
                        frames: workspaceTile.blinkFrames
                        duration: workspaceTile.blinkAnim.duration > 0 ? workspaceTile.blinkAnim.duration : 760
                        easingType: workspaceTile.blinkAnim.easing !== undefined
                            ? workspaceTile.blinkAnim.easing : Easing.InOutSine
                        iterations: workspaceTile.blinkAnim.iterations !== undefined
                            ? workspaceTile.blinkAnim.iterations : -1
                        running: workspaceTile.urgentBlinkEnabled
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
                    // Styled via button.focused::after; falls back to a translucent
                    // accent so themes without an explicit overlay still mark the active
                    // workspace. The peak opacity is the colour's alpha.
                    color: {
                        var bg = root.fillColorOf(workspaceTile.activeFillStyle)
                        return bg.length > 0 ? cssTheme.parseColor(bg)
                            : root.alphaColor(theme.accent, 0.22)
                    }
                    opacity: workspaceTile.activeProgress
                    // Skip rendering entirely when the fill is transparent (e.g. a theme
                    // that marks the active workspace with a top border, not a fill) —
                    // otherwise the fading overlay flashes black. Mirrors hoverFill.
                    visible: opacity > 0.0 && color.a > 0.0
                }

                // Top accent line for the active workspace. Opt-in: themes set
                // border-top-width/-color on `button.focused`. Lets a theme mark the
                // active workspace with just a top line instead of a filled background.
                Rectangle {
                    id: activeLine
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: parent.width
                    height: root.cssPixels(workspaceTile.cssStyle, "border-top-width", 0)
                    color: workspaceTile.cssStyle["border-top-color"]
                        ? cssTheme.parseColor(workspaceTile.cssStyle["border-top-color"])
                        : (workspaceTile.attention ? "#ff5555" : theme.accent)
                    opacity: workspaceTile.activeProgress
                    visible: height > 0 && opacity > 0.0
                }

                Rectangle {
                    id: hoverLine
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: parent.width * workspaceTile.hoverReveal
                    height: root.cssPixels(workspaceTile.cssStyle, "border-bottom-width", 2)
                    color: workspaceTile.cssStyle["border-bottom-color"]
                        ? cssTheme.parseColor(workspaceTile.cssStyle["border-bottom-color"])
                        : (workspaceTile.attention ? "#ff5555" : theme.accent)
                    opacity: workspaceTile.hoverOpacity

                    Behavior on width {
                        NumberAnimation {
                            duration: 120
                            easing.type: root.cssEasing(workspaceTile.cssStyle, "hover-easing", Easing.OutCubic)
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.cssDuration(workspaceTile.cssStyle, "hover-duration", 240)
                            easing.type: root.cssEasing(workspaceTile.cssStyle, "hover-easing", Easing.OutCubic)
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
                height: theme.height
                radius: 0
                clip: true
                property real hoverReveal: 0.0
                property real hoverOpacity: 0.0
                property bool attention: false
                readonly property var cssStyle: root.buttonStyle(index === 0 ? ["focused"] : index === 1 ? ["visible"] : [])
                readonly property real tilePaddingX: root.paddingX(cssStyle)
                readonly property real tileMinWidth: root.cssPixels(cssStyle, "min-width", 0)
                readonly property real tileRadius: root.cssPixels(cssStyle, "border-radius", 0)
                readonly property var tileTransition: root.transitionOf(cssStyle)
                readonly property color tileBg: cssStyle["background-color"]
                    ? cssTheme.parseColor(cssStyle["background-color"])
                    : ["#273847", "#526171", "#ff5555", "#35495c", "#4a596a"][index]
                readonly property color tileFg: cssStyle["color"]
                    ? cssTheme.parseColor(cssStyle["color"])
                    : (fallbackTile.attention ? "#ffffff" : "#eef2f7")

                width: Math.max(label.implicitWidth + tilePaddingX * 2, tileMinWidth)
                color: "transparent"

                QBar.CssFill {
                    anchors.fill: parent
                    style: fallbackTile.cssStyle
                    radius: fallbackTile.tileRadius
                    defaultColor: fallbackTile.tileBg
                    transitionMs: fallbackTile.tileTransition.ms
                    transitionEasingType: fallbackTile.tileTransition.easing
                }

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
                    height: root.cssPixels(fallbackTile.cssStyle, "border-bottom-width", 2)
                    color: fallbackTile.cssStyle["border-bottom-color"]
                        ? cssTheme.parseColor(fallbackTile.cssStyle["border-bottom-color"])
                        : (fallbackTile.attention ? "#ff5555" : theme.accent)
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
                    color: fallbackTile.tileFg
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
