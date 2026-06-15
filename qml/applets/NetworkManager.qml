import QtQuick
import QtQuick.Effects
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "network"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    readonly property color itemBackground: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#2f80b8"
    readonly property color effectiveBackground: Contrast.effectiveBackground(itemBackground, cssTheme, theme.background)
    readonly property color iconColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : Contrast.contrastColor(effectiveBackground)

    property string mode: networkManagerModel ? networkManagerModel.mode : "disconnected"
    property string iconName: networkManagerModel ? networkManagerModel.iconName : "network-wired-disconnected-symbolic"
    property string label: networkManagerModel ? networkManagerModel.label : ""
    property string interfaceName: networkManagerModel ? networkManagerModel.interfaceName : ""
    property string ipText: networkManagerModel ? networkManagerModel.ipText : ""
    property string ipv4Text: networkManagerModel ? networkManagerModel.ipv4Text : ""
    property string ipv6Text: networkManagerModel ? networkManagerModel.ipv6Text : ""
    property string tooltipText: networkManagerModel ? networkManagerModel.tooltipText : "network disconnected"
    property int strength: networkManagerModel ? networkManagerModel.strength : 0
    property int iconWidth: 18
    property int iconHeight: 18
    property int displayMode: 0
    property bool showIconBox: root.mode !== "disconnected"
    property string displayText: {
        if (root.displayMode === 1) {
            return root.ipText
        }
        if (root.displayMode === 2) {
            return root.ipv6Text
        }
        if (root.mode === "wireless" && root.displayMode === 0) {
            return root.label
        }
        return ""
    }
    property bool showText: displayText.length > 0
    property int preferredWidth: (showIconBox ? iconWidth : 0) + (showText ? labelText.implicitWidth + 6 : 0) + 10
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)
    onModeChanged: root.displayMode = 0
    onIpTextChanged: preferredWidthUpdated(preferredWidth)
    onIpv4TextChanged: preferredWidthUpdated(preferredWidth)
    onIpv6TextChanged: preferredWidthUpdated(preferredWidth)
    onLabelChanged: preferredWidthUpdated(preferredWidth)
    onDisplayModeChanged: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    Rectangle {
        anchors.fill: parent
        color: root.itemBackground
    }

    Item {
        id: iconBox
        width: root.iconWidth
        height: root.iconHeight
        visible: root.showIconBox
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
            visible: false
        }

        MultiEffect {
            anchors.fill: iconImage
            source: iconImage
            visible: iconImage.status === Image.Ready
            colorization: 1.0
            colorizationColor: root.iconColor
        }

        Canvas {
            id: iconFallback
            anchors.fill: parent
            visible: iconImage.status !== Image.Ready

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var iconColor = cssStyle["color"] || Contrast.contrastColor(root.effectiveBackground)
                ctx.strokeStyle = iconColor
                ctx.fillStyle = iconColor
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

            Connections {
                target: root
                function onCssStyleChanged() { iconFallback.requestPaint() }
            }
        }

        Connections {
            target: networkManagerModel
            function onStatusChanged() { iconFallback.requestPaint() }
        }
    }

    Text {
        id: labelText
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: root.showIconBox ? iconBox.right : parent.left
        anchors.leftMargin: root.showIconBox ? 4 : 5
        color: root.iconColor
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
        text: root.displayText
        visible: text.length > 0
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: {
            if (root.mode === "wireless" || root.mode === "wired") {
                root.displayMode = (root.displayMode + 1) % 3
            }
        }
    }
}
