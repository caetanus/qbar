import QtQuick
import "qrc:/qbar" as QBar

// i3/sway scratchpad indicator: shows how many windows are stashed in the
// scratchpad and cycles them on click (`scratchpad show`). Like waybar's module it
// collapses to nothing when the scratchpad is empty. Styled by the theme's
// #scratchpad rule.
QBar.CssRect {
    id: root
    cssId: "scratchpad"
    height: theme.height

    readonly property var cssStyle: root.style
    property int count: i3Ipc ? i3Ipc.scratchpadCount : 0
    readonly property bool hasWindows: count > 0

    // Sizing contract: the bar's Loader has anchors.fill and clears a `width:`
    // binding, so Bar.appletWidth reads preferredWidth. Drive that — and collapse
    // to 0 when the scratchpad is empty so the slot disappears entirely (a `visible`
    // binding wouldn't reactively reclaim the width).
    property int preferredWidth: hasWindows ? Math.max(1, content.implicitWidth + 14) : 0
    width: preferredWidth
    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 5
        visible: root.hasWindows

        Text {
            anchors.verticalCenter: parent.verticalCenter
            // window-restore glyph (Font Awesome / Nerd Font) — "stashed windows".
            text: ""
            color: root.cssStyle["color"] ? cssTheme.parseColor(root.cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
        QBar.CssText {
            cssId: "scratchpad"
            anchors.verticalCenter: parent.verticalCenter
            text: root.count
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (i3Ipc) i3Ipc.runCommand("scratchpad show")
    }
}
