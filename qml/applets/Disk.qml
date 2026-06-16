import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "disk"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    property bool available: diskModel ? diskModel.available : false
    property string displayText: diskModel ? diskModel.displayText : ""
    property string tooltipText: diskModel ? diskModel.tooltipText : "disk unavailable"
    property bool tooltipHovered: false
    property int mountCount: diskModel ? diskModel.mounts.length : 0
    property int popupColumns: mountCount > 1 ? 2 : 1
    property int popupRows: Math.max(1, Math.ceil(Math.max(1, mountCount) / popupColumns))
    property int popupHeight: 24 + 18 + 10 + (popupRows * 86) + Math.max(0, popupRows - 1) * 8
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: {
        if (diskModel && cssStyle["mount"]) {
            diskModel.path = cssStyle["mount"]
        }
        preferredWidthUpdated(preferredWidth)
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    QBar.Popup {
        id: diskPopup
        anchorItem: root
        source: "qrc:/popups/DiskPopup.qml"
        payload: ({ disk: diskModel, columns: root.popupColumns })
        popupWidth: 420
        popupHeight: root.popupHeight
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    Rectangle {
        anchors.fill: parent
        visible: root.available
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "transparent"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.available

        Item {
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter

            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var c = cssStyle["color"] || "#ffffff"
                    ctx.strokeStyle = c
                    ctx.fillStyle = c
                    ctx.lineWidth = 1.4

                    // A small stacked-disk (database) glyph.
                    function ellipse(cy) {
                        ctx.beginPath()
                        ctx.ellipse(1.5, cy, 11, 4)
                        ctx.stroke()
                    }
                    ctx.beginPath()
                    ctx.moveTo(1.5, 3)
                    ctx.lineTo(1.5, 11)
                    ctx.moveTo(12.5, 3)
                    ctx.lineTo(12.5, 11)
                    ctx.stroke()
                    ellipse(1)
                    ellipse(9)
                }

                Connections {
                    target: root
                    function onCssStyleChanged() { parent.requestPaint() }
                }
            }
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
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: diskPopup.toggle()
    }
}
