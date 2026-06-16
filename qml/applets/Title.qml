import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    width: barWidth > 0 ? barWidth : 380
    height: theme.height

    readonly property string cssId: "title"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    property real barWidth: 380
    property real leftOccupiedWidth: 0
    property real rightOccupiedWidth: 0
    property int textPadding: 8
    property real titleContentWidth: 0
    property real naturalWidth: Math.ceil(root.titleContentWidth + root.textPadding * 2)
    property real availableWidth: Math.max(0, root.barWidth - root.leftOccupiedWidth - root.rightOccupiedWidth)
    property bool overlap: (root.barWidth / 2 - root.naturalWidth / 2) < root.leftOccupiedWidth
        || (root.barWidth / 2 + root.naturalWidth / 2) > (root.barWidth - root.rightOccupiedWidth)
    property real boxWidth: overlap ? Math.min(root.naturalWidth, root.availableWidth) : Math.min(root.naturalWidth, root.barWidth)
    property real absoluteX: Math.max(0, Math.min(root.barWidth - root.boxWidth, (root.barWidth - root.boxWidth) / 2))
    property real relativeCenterX: root.leftOccupiedWidth + (root.availableWidth - root.boxWidth) / 2
    property real relativeX: Math.max(root.leftOccupiedWidth, Math.min(root.barWidth - root.rightOccupiedWidth - root.boxWidth, root.relativeCenterX))
    property real boxX: overlap ? root.relativeX : root.absoluteX

    signal activated()

    Rectangle {
        x: root.boxX
        width: root.boxWidth
        height: root.height
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#665b6978"
    }

    Text {
        id: titleText
        x: root.boxX
        width: root.boxWidth
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: root.textPadding
        rightPadding: root.textPadding

        readonly property var dropShadow: cssTheme && cssTheme.loaded
            ? cssTheme.parseBoxShadow(root.cssStyle["text-shadow"] || "") : ({})
        layer.enabled: dropShadow.color !== undefined
        layer.effect: QBar.CssDropShadow { shadow: titleText.dropShadow }

        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        text: i3Ipc && i3Ipc.currentWindowTitle.length > 0 ? i3Ipc.currentWindowTitle : ""
        onImplicitWidthChanged: root.titleContentWidth = implicitWidth
        Component.onCompleted: root.titleContentWidth = implicitWidth
    }
}
