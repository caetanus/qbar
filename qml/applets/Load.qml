import QtQuick
import "qrc:/qbar" as QBar

// System load averages (waybar's "load"). Shows the 1-minute average; the
// tooltip carries all three. CSS: #load { color, font-family, ... }.
QBar.CssRect {
    id: root
    cssId: "load"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property bool available: loadModel ? loadModel.available : false
    property string displayText: loadModel ? loadModel.displayText : ""
    property string tooltipText: loadModel ? loadModel.tooltipText : ""
    property bool tooltipHovered: false
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.available

        QBar.CssText {
            anchors.verticalCenter: parent.verticalCenter
            cssId: ""
            text: "⌀"
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
            text: root.displayText
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onContainsMouseChanged: root.tooltipHovered = containsMouse
    }
}
