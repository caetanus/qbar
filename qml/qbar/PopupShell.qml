import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root

    // Whether the active theme is light, from theme.background luminance. Drives
    // the popup's default chrome/foreground so it reads on light *and* dark themes.
    readonly property color themeBackground: cssTheme && cssTheme.loaded
        ? Contrast.barBackground(cssTheme, theme.background)
        : (cssTheme ? cssTheme.parseColor(theme.background) : theme.background)
    readonly property bool themeIsLight: Contrast.luminance(themeBackground) >= 0.5
    readonly property color defaultPopupBackground: themeIsLight
        ? Qt.rgba(0.96, 0.96, 0.97, 0.94)
        : Qt.rgba(0.11, 0.11, 0.13, 0.92)
    readonly property color defaultPopupForeground: themeIsLight ? "#1a1a1a" : "#ffffff"

    property int targetWidth: 1
    property int targetHeight: 1
    property bool popupClosing: false
    property bool animateBounds: true

    readonly property int growDuration: animateBounds
        ? (theme.animationDuration > 0 ? theme.animationDuration : 180) : 0

    width: Math.max(1, targetWidth)
    height: Math.max(1, targetHeight)
    clip: true

    // Grow-in animation: scale from the center + fade. Lets us judge whether the
    // single-backdrop architecture keeps popup animations fluid. Tooltips
    // (animateBounds:false) just appear without the scale pop.
    transformOrigin: Item.Center
    opacity: 0
    scale: animateBounds ? 0.9 : 1.0

    Behavior on opacity { NumberAnimation { duration: root.growDuration; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: root.growDuration; easing.type: Easing.OutBack } }

    function applyVisibility() {
        if (popupClosing) {
            opacity = 0
            scale = animateBounds ? 0.9 : 1.0
        } else {
            opacity = root.shownOpacity
            scale = 1.0
        }
    }

    onPopupClosingChanged: applyVisibility()
    onShownOpacityChanged: if (!popupClosing) applyVisibility()
    Component.onCompleted: applyVisibility()

    // CSS-configurable popup chrome (#popup): solid color, linear-gradient (any
    // angle) and border via CssFill. Default: a neutral dark, slightly
    // translucent (readable but not fully opaque, and not the bar's blue tint).
    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})

    // CSS `opacity` on #popup — applies to the whole popup (chrome + content),
    // matching real CSS. The grow animation fades to this resting value.
    readonly property real shownOpacity: popupStyle["opacity"] !== undefined
        ? parseFloat(popupStyle["opacity"]) : 1.0

    QBar.CssFill {
        anchors.fill: parent
        style: root.popupStyle
        radius: 2
        defaultColor: root.defaultPopupBackground
        defaultBorderColor: root.themeIsLight ? Qt.rgba(0, 0, 0, 0.15) : Qt.rgba(1, 1, 1, 0.18)
        defaultBorderWidth: 1
    }

    // Absorb clicks landing on the popup chrome so they don't reach the
    // backdrop's dismiss MouseArea below. Sits under contentLayer, so the
    // popup content's own interactive areas still receive events first.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: function(mouse) { mouse.accepted = true }
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Item {
        id: contentLayer
        width: Math.max(1, root.targetWidth)
        height: Math.max(1, root.targetHeight)

        Loader {
            id: loader
            objectName: "qbarPopupLoader"
            anchors.fill: parent
            source: contentSource
            asynchronous: false
            // Let focusable popup content (e.g. the calendar's keyboard date
            // navigation) take activeFocus when the overlay holds the keyboard.
            focus: true

            onLoaded: {
                if (!item) {
                    return
                }
                for (var key in popupData) {
                    if (Object.prototype.hasOwnProperty.call(popupData, key)) {
                        item[key] = popupData[key]
                    }
                }
                Qt.callLater(function() {
                    if (loader.item) {
                        loader.item.forceActiveFocus()
                    }
                })
            }
        }
    }
}
