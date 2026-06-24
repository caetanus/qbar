import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

QBar.CssRect {
    id: root
    cssId: "caffeine"
    // State → cssClass: the engine resolves #caffeine / #caffeine.active and pushes the
    // per-state background (the reverse slot re-applies when `active` flips).
    cssClass: root.active ? ["active"] : []
    width: 32
    height: theme.height

    readonly property var cssStyle: root.style

    property bool active: caffeineModel ? caffeineModel.active : false

    // Falls through to transparent (no badge) when the theme leaves #caffeine unstyled;
    // the icon's contrast then resolves against the bar background, staying readable.
    readonly property color backgroundColor: cssStyle["background-color"]
        ? cssTheme.parseColor(cssStyle["background-color"])
        : "transparent"

    readonly property color iconColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : Contrast.contrastColor(Contrast.effectiveBackground(root.backgroundColor, cssTheme, theme.background))

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.active ? "disable screen lock" : "enable screen lock"
        side: "auto"
    }

    // Background painted by the CssRect base (per-state via cssClass).

    QBar.CssIcon {
        anchors.centerIn: parent
        width: 23
        height: 23
        style: root.cssStyle
        fallbackSource: root.active
            ? "qrc:/icons/caffeine-cup-full-white.svg"
            : "qrc:/icons/caffeine-cup-empty-white.svg"
        color: root.iconColor
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (caffeineModel) {
                caffeineModel.toggle()
            }
        }
    }
}
