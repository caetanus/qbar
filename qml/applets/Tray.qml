import QtQuick
import QtQuick.Effects
import "qrc:/qbar/Contrast.js" as Contrast

Row {
    id: root
    height: theme.height
    spacing: 0
    width: preferredWidth

    readonly property string cssId: "tray"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

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
            id: trayItem
            width: root.itemWidth
            height: theme.height

            readonly property bool hasIcon: iconSource && iconSource.length > 0
            readonly property bool hasOverlay: hasIcon && overlayIconName && overlayIconName.length > 0
            readonly property bool hasSymbolicIcon: symbolicIconSource && symbolicIconSource.length > 0
            readonly property bool hasOverlaySymbolicIcon: overlaySymbolicIconSource && overlaySymbolicIconSource.length > 0

            readonly property color itemBackground: status === "NeedsAttention"
                ? (root.cssStyle["attention-background"] ? cssTheme.parseColor(root.cssStyle["attention-background"]) : "#ccbd4b4b")
                : (root.cssStyle["background-color"] ? cssTheme.parseColor(root.cssStyle["background-color"]) : "#2f80b8")
            readonly property color effectiveBackground: Contrast.effectiveBackground(itemBackground, cssTheme, theme.background)
            readonly property color iconColor: root.cssStyle["color"]
                ? cssTheme.parseColor(root.cssStyle["color"])
                : Contrast.contrastColor(effectiveBackground)
            // Icons are usually full-color brand logos, not symbolic glyphs, and most
            // are drawn light-on-dark — they read fine on a dark background as-is.
            // Only recolor when this background is light enough to need a dark
            // contrast color, since native light icons would disappear there.
            // (This only applies when no "-symbolic" variant is available — see
            // hasSymbolicIcon below, which is preferred whenever the icon theme
            // provides one.)
            readonly property bool recolorIcon: root.cssStyle["color"]
                ? true
                : Contrast.needsDarkIcon(effectiveBackground)

            color: itemBackground

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
                visible: false
                source: trayItem.hasSymbolicIcon ? symbolicIconSource : (trayItem.hasIcon ? iconSource : "")
            }

            MultiEffect {
                anchors.fill: trayIcon
                source: trayIcon
                visible: trayItem.hasIcon
                colorization: (trayItem.hasSymbolicIcon || trayItem.recolorIcon) ? 1.0 : 0.0
                colorizationColor: trayItem.iconColor
            }

            Text {
                anchors.centerIn: parent
                visible: !trayItem.hasIcon
                color: trayItem.iconColor
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                text: title.slice(0, 1).toUpperCase()
            }

            Image {
                id: overlayIcon
                anchors.right: trayIcon.right
                anchors.bottom: trayIcon.bottom
                width: Math.max(8, trayIcon.width / 2)
                height: width
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                visible: false
                source: trayItem.hasOverlaySymbolicIcon ? overlaySymbolicIconSource : (trayItem.hasOverlay ? "image://themeicon/" + overlayIconName : "")
            }

            MultiEffect {
                anchors.fill: overlayIcon
                source: overlayIcon
                visible: trayItem.hasOverlay
                colorization: (trayItem.hasOverlaySymbolicIcon || trayItem.recolorIcon) ? 1.0 : 0.0
                colorizationColor: trayItem.iconColor
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
