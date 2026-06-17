import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    width: 32
    height: theme.height

    readonly property string cssId: "caffeine"
    readonly property var cssClasses: root.active ? ["active"] : []
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId, cssClasses) : ({})

    property bool active: caffeineModel ? caffeineModel.active : false

    readonly property color backgroundColor: cssStyle["background-color"]
        ? cssTheme.parseColor(cssStyle["background-color"])
        : (root.active ? "#ffffff" : "#000000")

    readonly property color iconColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : Contrast.contrastColor(Contrast.effectiveBackground(root.backgroundColor, cssTheme, theme.background))

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.active ? "disable screen lock" : "enable screen lock"
        side: "auto"
    }

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
    }

    QBar.CssIcon {
        anchors.centerIn: parent
        width: 23
        height: 23
        style: root.cssStyle
        fallbackSource: root.active
            ? "qrc:/icons/caffeine-cup-full-black.svg"
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
