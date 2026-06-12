import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    property string mode: networkManagerModel ? networkManagerModel.mode : "disconnected"
    property string iconName: networkManagerModel ? networkManagerModel.iconName : "network-wired-disconnected-symbolic"
    property string label: networkManagerModel ? networkManagerModel.label : ""
    property string interfaceName: networkManagerModel ? networkManagerModel.interfaceName : ""
    property string tooltipText: networkManagerModel ? networkManagerModel.tooltipText : "network disconnected"
    property int strength: networkManagerModel ? networkManagerModel.strength : 0
    property int iconWidth: 18
    property int iconHeight: 18
    property int preferredWidth: iconWidth + (labelText.visible ? labelText.implicitWidth + 6 : 0) + 10
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    Rectangle {
        anchors.fill: parent
        color: "#2f80b8"
    }

    Item {
        id: iconBox
        width: root.iconWidth
        height: root.iconHeight
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 5

        Image {
            id: iconImage
            anchors.fill: parent
            source: "image://themeicon/" + root.iconName
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: status === Image.Ready
        }

        Canvas {
            id: iconFallback
            anchors.fill: parent
            visible: iconImage.status !== Image.Ready

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#ffffff"
                ctx.fillStyle = "#ffffff"
                ctx.lineWidth = 1.8
                ctx.lineCap = "round"
                ctx.lineJoin = "round"

                if (root.mode === "wireless") {
                    var cx = width / 2
                    var cy = height - 4
                    ctx.beginPath()
                    ctx.arc(cx, cy, 1.4, 0, Math.PI * 2)
                    ctx.fill()

                    var levels = Math.max(1, Math.min(4, Math.round(root.strength / 25)))
                    for (var i = 1; i <= 3; ++i) {
                        ctx.save()
                        ctx.globalAlpha = i <= levels ? 1.0 : 0.35
                        ctx.beginPath()
                        ctx.arc(cx, cy, 2.8 + i * 2.15, Math.PI * 1.05, Math.PI * 1.95)
                        ctx.stroke()
                        ctx.restore()
                    }
                } else {
                    ctx.beginPath()
                    ctx.rect(3.5, 4, 11, 9)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(6, 7)
                    ctx.lineTo(6, 11)
                    ctx.moveTo(9, 7)
                    ctx.lineTo(9, 11)
                    ctx.moveTo(12, 7)
                    ctx.lineTo(12, 11)
                    ctx.stroke()

                    if (root.mode === "disconnected") {
                        ctx.beginPath()
                        ctx.moveTo(4, 4)
                        ctx.lineTo(14, 14)
                        ctx.moveTo(14, 4)
                        ctx.lineTo(4, 14)
                        ctx.stroke()
                    }
                }
            }

            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }

        Connections {
            target: networkManagerModel
            function onStatusChanged() { iconFallback.requestPaint() }
        }
    }

    Text {
        id: labelText
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: iconBox.right
        anchors.leftMargin: 4
        color: "#ffffff"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
        text: root.label
        visible: text.length > 0
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: root.tooltipHovered = containsMouse
    }
}
