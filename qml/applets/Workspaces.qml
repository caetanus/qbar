import QtQuick
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

    function cssDuration(style, key, fallback) {
        return cssTheme && cssTheme.loaded ? cssTheme.parseDuration(style ? (style[key] || "") : "", fallback) : fallback
    }

    function cssEasing(style, key, fallback) {
        return cssTheme && cssTheme.loaded ? cssTheme.parseEasing(style ? (style[key] || "") : "", fallback) : fallback
    }

    QBar.CssFill {
        anchors.fill: parent
        style: root.cssStyle
        radius: root.containerRadius
        defaultColor: "transparent"
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
                readonly property real activeFillOpacity: root.cssPixels(cssStyle, "active-fill-opacity", 0.22)
                readonly property int transitionDuration: root.cssDuration(cssStyle, "transition-duration", 180)
                readonly property int transitionEasing: root.cssEasing(cssStyle, "transition-easing", Easing.InOutQuad)
                readonly property int activeDuration: root.cssDuration(cssStyle, "active-duration", 240)
                readonly property int activeEasing: root.cssEasing(cssStyle, "active-easing", Easing.InOutCubic)
                readonly property int animationDuration: root.cssDuration(cssStyle, "animation-duration", 900)
                readonly property int animationEasing: root.cssEasing(cssStyle, "animation-easing", Easing.InOutSine)
                readonly property bool urgentBlinkEnabled: attention && cssStyle["urgent-blink-color"]
                readonly property color tileBg: cssStyle["background-color"]
                    ? cssTheme.parseColor(cssStyle["background-color"])
                    : (workspaceTile.attention ? "#ff5555" : visible ? "#526171" : "#273847")
                readonly property color tileFg: cssStyle["color"]
                    ? cssTheme.parseColor(cssStyle["color"])
                    : (workspaceTile.attention ? "#ffffff" : "#eef2f7")

                width: Math.max(label.implicitWidth + tilePaddingX * 2, tileMinWidth)
                color: "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: workspaceTile.transitionDuration
                        easing.type: workspaceTile.transitionEasing
                    }
                }

                QBar.CssFill {
                    anchors.fill: parent
                    style: workspaceTile.cssStyle
                    radius: workspaceTile.tileRadius
                    defaultColor: workspaceTile.tileBg
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
                    color: workspaceTile.cssStyle["urgent-blink-color"]
                        ? cssTheme.parseColor(workspaceTile.cssStyle["urgent-blink-color"])
                        : "transparent"
                    opacity: 0.0
                    visible: workspaceTile.urgentBlinkEnabled

                    SequentialAnimation on opacity {
                        running: workspaceTile.urgentBlinkEnabled
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: 0.0
                            to: root.cssPixels(workspaceTile.cssStyle, "urgent-blink-opacity", 0.85)
                            duration: Math.max(1, workspaceTile.animationDuration / 2)
                            easing.type: workspaceTile.animationEasing
                        }
                        NumberAnimation {
                            from: root.cssPixels(workspaceTile.cssStyle, "urgent-blink-opacity", 0.85)
                            to: 0.0
                            duration: Math.max(1, workspaceTile.animationDuration / 2)
                            easing.type: workspaceTile.animationEasing
                        }
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
                    color: workspaceTile.cssStyle["active-fill-color"]
                        ? cssTheme.parseColor(workspaceTile.cssStyle["active-fill-color"])
                        : theme.accent
                    opacity: workspaceTile.activeProgress * workspaceTile.activeFillOpacity
                    visible: opacity > 0.0
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
                readonly property int transitionDuration: root.cssDuration(cssStyle, "transition-duration", 180)
                readonly property int transitionEasing: root.cssEasing(cssStyle, "transition-easing", Easing.InOutQuad)
                readonly property color tileBg: cssStyle["background-color"]
                    ? cssTheme.parseColor(cssStyle["background-color"])
                    : ["#273847", "#526171", "#ff5555", "#35495c", "#4a596a"][index]
                readonly property color tileFg: cssStyle["color"]
                    ? cssTheme.parseColor(cssStyle["color"])
                    : (fallbackTile.attention ? "#ffffff" : "#eef2f7")

                width: Math.max(label.implicitWidth + tilePaddingX * 2, tileMinWidth)
                color: "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: fallbackTile.transitionDuration
                        easing.type: fallbackTile.transitionEasing
                    }
                }

                QBar.CssFill {
                    anchors.fill: parent
                    style: fallbackTile.cssStyle
                    radius: fallbackTile.tileRadius
                    defaultColor: fallbackTile.tileBg
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
