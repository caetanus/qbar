import QtQuick

Row {
    id: root
    height: theme.height
    spacing: 0
    width: preferredWidth

    property int iconSize: Math.max(16, Math.round((theme.height - 6) * 0.85))
    property real itemPadding: Math.max(2, Math.round(theme.trayItemPadding * 2))
    property real itemWidth: iconSize + itemPadding * 2
    property int preferredWidth: trayModel ? trayModel.count * itemWidth : 0

    signal activated()
    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    Repeater {
        model: trayModel ? trayModel : 0

        Rectangle {
            width: root.itemWidth
            height: theme.height
            color: status === "NeedsAttention" ? "#ccbd4b4b" : "#2f80b8"

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
                visible: iconSource && iconSource.length > 0
                source: visible ? iconSource : ""
            }

            Text {
                anchors.centerIn: parent
                visible: !trayIcon.visible
                color: theme.foreground
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                text: title.slice(0, 1).toUpperCase()
            }

            Image {
                anchors.right: trayIcon.right
                anchors.bottom: trayIcon.bottom
                width: Math.max(8, trayIcon.width / 2)
                height: width
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                visible: trayIcon.visible && overlayIconName && overlayIconName.length > 0
                source: visible ? "image://themeicon/" + overlayIconName : ""
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
