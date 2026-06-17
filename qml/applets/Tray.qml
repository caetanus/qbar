import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    height: theme.height
    width: preferredWidth

    readonly property string cssId: "tray"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})
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
    property real itemPadding: Math.max(2, Math.round(theme.trayItemPadding * 2))
    property real itemWidth: iconSize + itemPadding * 2
    property int preferredWidth: trayModel ? Math.ceil(trayModel.count * itemWidth + paddingLeft + paddingRight) : 0

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

    QBar.CssFill {
        anchors.fill: parent
        style: root.cssStyle
        radius: root.cssStyle["border-radius"] ? cssTheme.parseLength(root.cssStyle["border-radius"], 0) : 0
    }

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

        Repeater {
            model: trayModel ? trayModel : 0

            Rectangle {
                id: trayItem
                width: root.itemWidth
                height: contentRow.height

                readonly property bool hasIcon: iconSource && iconSource.length > 0
                readonly property bool hasOverlay: hasIcon && overlayIconName && overlayIconName.length > 0
                readonly property bool hasSymbolicIcon: symbolicIconSource && symbolicIconSource.length > 0
                readonly property bool hasOverlaySymbolicIcon: overlaySymbolicIconSource && overlaySymbolicIconSource.length > 0

                readonly property color itemBackground: status === "NeedsAttention"
                    ? (root.cssStyle["attention-background"] ? cssTheme.parseColor(root.cssStyle["attention-background"]) : "#ccbd4b4b")
                    : (root.cssStyle["item-background-color"] ? cssTheme.parseColor(root.cssStyle["item-background-color"]) : "transparent")
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
