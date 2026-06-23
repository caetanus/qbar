import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

QBar.CssRect {
    id: root
    cssId: "tray"
    height: theme.height
    width: preferredWidth

    // The engine pushes the resolved #tray rules into CssRect's `style` sink.
    readonly property var cssStyle: root.style
    // Per-item background via the standard-CSS `#tray.item { background-color }` part;
    // the attention state is `#tray.item:urgent { background-color }`.
    readonly property var itemStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "item") : ({})
    readonly property var itemUrgentStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "item", ["urgent"]) : ({})
    // How tray icons are rendered (CSS `icon-mode`):
    //   "normal" — native full-colour icons, never recoloured (Windows-like)
    //   "light"  — recoloured white   |  "dark" — recoloured near-black
    //   "auto"   — recolour only symbolic icons / when the background needs it
    readonly property string iconMode: (root.cssStyle["icon-mode"] || "auto").toString().toLowerCase()
    readonly property real paddingTop: cssLengthFromList("padding", 0, 0)
    readonly property real paddingRight: cssLengthFromList("padding", 1, 0)
    readonly property real paddingBottom: cssLengthFromList("padding", 2, 0)
    readonly property real paddingLeft: cssLengthFromList("padding", 3, 0)

    property int iconSize: Math.max(16, Math.round((theme.height - 6) * 0.85))
    // Per-icon padding (each side). waybar keeps tray icons tight (gaps come from `spacing`),
    // so this is just `trayItemPadding`, NOT doubled — the ×2 made the tray look over-padded.
    property real itemPadding: Math.max(1, Math.round(theme.trayItemPadding))
    property real itemWidth: iconSize + itemPadding * 2

    // Collapsible tray (Windows-XP "show hidden icons"): when `#tray { -qbar-collapsible: true }`
    // the tray shows only a toggle chevron plus up to `-qbar-collapsed-max` (default 2)
    // attention-grabbing icons; clicking the chevron grows/shrinks the full tray. The grow is
    // CSS-driven — `#tray { transition: <dur> }` sets the duration/easing (QML easing).
    readonly property bool collapsible: String(cssStyle["-qbar-collapsible"] || "").toLowerCase() === "true"
    readonly property int collapsedMax: cssStyle["-qbar-collapsed-max"]
        ? Math.round(cssTheme.parseLength(cssStyle["-qbar-collapsed-max"], 2)) : 2
    property bool expanded: false
    // Model rows in the NeedsAttention state (reactive — the model re-emits on any change);
    // while collapsed, the first `collapsedMax` of these stay visible.
    readonly property var attentionRows: trayModel ? trayModel.attentionRows : []
    // Up to `collapsedMax` icons stay visible while collapsed: attention icons first, then the
    // earliest regular icons fill any remaining slots — so the tray never collapses to just
    // the chevron when nothing is demanding attention.
    readonly property var collapsedVisible: {
        var out = []
        for (var a = 0; a < attentionRows.length && out.length < collapsedMax; ++a)
            out.push(attentionRows[a])
        var total = trayModel ? trayModel.count : 0
        for (var i = 0; i < total && out.length < collapsedMax; ++i)
            if (out.indexOf(i) < 0)
                out.push(i)
        return out
    }
    readonly property real toggleWidth: collapsible ? Math.round(iconSize * 0.8) : 0
    readonly property var _trans: (cssTheme && cssTheme.loaded && cssStyle["transition"])
        ? cssTheme.parseTransition(cssStyle["transition"]) : ({})
    readonly property int transitionMs: _trans.duration !== undefined ? _trans.duration
        : (theme.animationDuration > 0 ? theme.animationDuration : 200)
    readonly property int transitionEasing: _trans.easing !== undefined ? _trans.easing : Easing.OutCubic
    readonly property color toggleColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : Contrast.contrastColor(Contrast.effectiveBackground(
            cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : theme.background,
            cssTheme, theme.background))

    // While collapsed, only the toggle + the first `collapsedMax` attention icons show.
    function itemShown(i) {
        if (!collapsible || expanded)
            return true
        return collapsedVisible.indexOf(i) >= 0
    }

    // The applet sizes itself to its content (toggle + visible icons), so it grows/shrinks
    // as the drawer opens; the slot width follows preferredWidth.
    property int preferredWidth: Math.ceil(contentRow.implicitWidth + paddingLeft + paddingRight)

    signal activated()
    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function cssBoxParts(name) {
        var raw = cssStyle[name] || ""
        return raw.toString().trim().split(/\s+/).filter(function(part) { return part.length > 0 })
    }

    function cssLengthFromList(name, index, fallback) {
        var parts = cssBoxParts(name)
        if (parts.length === 0 || !cssTheme) {
            return fallback
        }
        var value = parts[0]
        if (parts.length === 2) {
            value = index % 2 === 0 ? parts[0] : parts[1]
        } else if (parts.length === 3) {
            value = index === 3 ? parts[1] : parts[index]
        } else if (parts.length >= 4) {
            value = parts[index]
        }
        return cssTheme.parseLength(value, fallback)
    }

    // Background is painted by the CssRect base; per-item backgrounds are below.
    Row {
        id: contentRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.paddingLeft
        anchors.rightMargin: root.paddingRight
        anchors.topMargin: root.paddingTop
        anchors.bottomMargin: root.paddingBottom
        spacing: root.cssStyle["spacing"] ? cssTheme.parseLength(root.cssStyle["spacing"], 0) : 0

        // Toggle chevron — the XP "show hidden icons" arrow. ‹ collapsed (click to reveal),
        // › expanded (click to hide). Only present when the tray is collapsible.
        Item {
            id: toggle
            width: root.collapsible ? root.toggleWidth : 0
            height: contentRow.height
            visible: root.collapsible
            clip: true

            Text {
                anchors.centerIn: parent
                text: root.expanded ? "›" : "‹"  // › / ‹
                color: root.toggleColor
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 3
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expanded = !root.expanded
            }
        }

        Repeater {
            model: trayModel ? trayModel : 0

            Rectangle {
                id: trayItem
                readonly property bool shown: root.itemShown(index)
                width: shown ? root.itemWidth : 0
                clip: true
                height: contentRow.height

                Behavior on width {
                    enabled: root.collapsible
                    NumberAnimation {
                        duration: root.transitionMs
                        easing.type: root.transitionEasing
                    }
                }

                readonly property bool hasIcon: iconSource && iconSource.length > 0
                readonly property bool hasOverlay: hasIcon && overlayIconName && overlayIconName.length > 0
                readonly property bool hasSymbolicIcon: symbolicIconSource && symbolicIconSource.length > 0
                readonly property bool hasOverlaySymbolicIcon: overlaySymbolicIconSource && overlaySymbolicIconSource.length > 0

                readonly property color itemBackground: status === "NeedsAttention"
                    ? (root.itemUrgentStyle["background-color"] ? cssTheme.parseColor(root.itemUrgentStyle["background-color"]) : "#ccbd4b4b")
                    : (root.itemStyle["background-color"] ? cssTheme.parseColor(root.itemStyle["background-color"]) : "transparent")
                readonly property color effectiveBackground: Contrast.effectiveBackground(
                    itemBackground.a > 0 ? itemBackground : (root.cssStyle["background-color"] ? cssTheme.parseColor(root.cssStyle["background-color"]) : theme.background),
                    cssTheme,
                    theme.background)
                readonly property color iconColor:
                    root.iconMode === "light" ? "#ffffff"
                    : root.iconMode === "dark" ? "#2b2b2b"
                    : (root.cssStyle["color"]
                        ? cssTheme.parseColor(root.cssStyle["color"])
                        : Contrast.contrastColor(effectiveBackground))
                // Icons are usually full-color brand logos, not symbolic glyphs, and most
                // are drawn light-on-dark — they read fine on a dark background as-is.
                // Only recolor when this background is light enough to need a dark
                // contrast color, since native light icons would disappear there.
                // (This only applies when no "-symbolic" variant is available — see
                // hasSymbolicIcon below, which is preferred whenever the icon theme
                // provides one.)
                readonly property bool recolorIcon:
                    root.iconMode === "normal" ? false
                    : (root.iconMode === "light" || root.iconMode === "dark") ? true
                    : (root.cssStyle["color"] ? true : Contrast.needsDarkIcon(effectiveBackground))
                // Ordered icon sources to try. "normal" prefers the full-colour
                // icon; other modes prefer the symbolic glyph. Either way we keep
                // the other variant as a fallback so a missing or broken-to-fetch
                // icon (e.g. Telegram) still renders instead of vanishing.
                readonly property var iconSources: {
                    var color = trayItem.hasIcon ? [iconSource] : []
                    var sym = trayItem.hasSymbolicIcon ? [symbolicIconSource] : []
                    return root.iconMode === "normal" ? color.concat(sym) : sym.concat(color)
                }
                property int iconStep: 0
                onIconSourcesChanged: iconStep = 0
                readonly property string activeIconSource: iconStep < iconSources.length ? iconSources[iconStep] : ""
                readonly property bool activeIconIsSymbolic: activeIconSource.length > 0
                    && activeIconSource === symbolicIconSource
                readonly property bool iconResolved: activeIconSource.length > 0
                // Last candidate also failed to load → fall through to first-letter text.
                readonly property bool iconBroken: iconResolved
                    && trayIcon.status === Image.Error
                    && iconStep >= iconSources.length - 1

                // A symbolic glyph always needs colourising; a full-colour icon only
                // when the mode/background asks for it.
                readonly property bool colorizeIcon: activeIconIsSymbolic
                    ? true : (root.iconMode === "normal" ? false : recolorIcon)
                readonly property bool colorizeOverlay: root.iconMode === "normal"
                    ? false : (hasOverlaySymbolicIcon || recolorIcon)

                color: itemBackground

                Image {
                    id: trayIcon
                    anchors.centerIn: parent
                    width: root.iconSize
                    height: root.iconSize
                    sourceSize.width: root.iconSize
                    sourceSize.height: root.iconSize
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    visible: false
                    source: trayItem.activeIconSource
                    // On a fetch/decode failure, advance to the next candidate source.
                    onStatusChanged: if (status === Image.Error
                        && trayItem.iconStep < trayItem.iconSources.length - 1) {
                        trayItem.iconStep++
                    }
                }

                MultiEffect {
                    anchors.fill: trayIcon
                    source: trayIcon
                    visible: trayItem.iconResolved && !trayItem.iconBroken
                    colorization: trayItem.colorizeIcon ? 1.0 : 0.0
                    colorizationColor: trayItem.iconColor
                }

                Text {
                    anchors.centerIn: parent
                    visible: !trayItem.iconResolved || trayItem.iconBroken
                    color: trayItem.iconColor
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    text: title.slice(0, 1).toUpperCase()
                }

                Image {
                    id: overlayIcon
                    anchors.right: trayIcon.right
                    anchors.bottom: trayIcon.bottom
                    width: Math.max(8, trayIcon.width / 2)
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    visible: false
                    source: (root.iconMode !== "normal" && trayItem.hasOverlaySymbolicIcon)
                        ? overlaySymbolicIconSource : (trayItem.hasOverlay ? "image://themeicon/" + overlayIconName : "")
                }

                MultiEffect {
                    anchors.fill: overlayIcon
                    source: overlayIcon
                    visible: trayItem.hasOverlay
                    colorization: trayItem.colorizeOverlay ? 1.0 : 0.0
                    colorizationColor: trayItem.iconColor
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor

                    function menuPoint() {
                        if (typeof mouseArea.mapToGlobal === "function") {
                            return mouseArea.mapToGlobal(Qt.point(0, mouseArea.height))
                        }
                        return Qt.point(0, 0)
                    }

                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            var point = menuPoint()
                            trayModel.contextMenuAt(index, point.x, point.y)
                        } else if (mouse.button === Qt.MiddleButton) {
                            trayModel.secondaryActivate(index)
                        } else {
                            var activatePoint = menuPoint()
                            trayModel.activateAt(index, activatePoint.x, activatePoint.y)
                        }
                    }
                    onDoubleClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            var point = menuPoint()
                            trayModel.contextMenuAt(index, point.x, point.y)
                        } else if (mouse.button === Qt.MiddleButton) {
                            trayModel.secondaryActivate(index)
                        } else {
                            var activatePoint = menuPoint()
                            trayModel.activateAt(index, activatePoint.x, activatePoint.y)
                        }
                    }
                    onWheel: function(wheel) {
                        var horizontal = Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y)
                        var delta = horizontal ? wheel.angleDelta.x : wheel.angleDelta.y
                        if (delta !== 0) {
                            trayModel.scroll(index, delta > 0 ? -1 : 1, horizontal ? "horizontal" : "vertical")
                            wheel.accepted = true
                        }
                    }
                }
            }
        }
    }
}
