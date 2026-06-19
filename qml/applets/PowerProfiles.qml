import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "power-profiles-daemon"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property bool available: powerProfilesModel ? powerProfilesModel.available : false
    property string activeProfile: powerProfilesModel ? powerProfilesModel.activeProfile : ""
    property string displayText: powerProfilesModel ? powerProfilesModel.displayText : ""
    property string tooltipText: powerProfilesModel ? powerProfilesModel.tooltipText : "power-profiles-daemon unavailable"
    property bool tooltipHovered: false
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    readonly property color iconColor: {
        if (cssStyle["color"]) return cssTheme.parseColor(cssStyle["color"])
        if (activeProfile === "performance") return "#fe640b"
        if (activeProfile === "power-saver") return "#40a02b"
        return theme.foreground
    }

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    // Background painted by the CssRect base.

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.available

        Item {
            width: 15
            height: 15
            anchors.verticalCenter: parent.verticalCenter

            QBar.CssIcon {
                id: cssIcon
                anchors.fill: parent
                style: root.cssStyle
                color: root.iconColor
                visible: hasCustomIcon
            }

            Canvas {
                id: icon
                anchors.fill: parent
                visible: !cssIcon.hasCustomIcon
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = root.iconColor
                    ctx.fillStyle = root.iconColor
                    ctx.lineWidth = 1.4
                    ctx.lineJoin = "round"
                    ctx.lineCap = "round"

                    // A gauge dial with a needle whose angle reflects the profile.
                    var cx = 7.5, cy = 9.5, r = 5.5
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, Math.PI, 2 * Math.PI)
                    ctx.stroke()

                    var t = root.activeProfile === "power-saver" ? 0.18
                          : (root.activeProfile === "performance" ? 0.82 : 0.5)
                    var ang = Math.PI + t * Math.PI
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.lineTo(cx + Math.cos(ang) * (r - 1), cy + Math.sin(ang) * (r - 1))
                    ctx.stroke()
                }
                Connections {
                    target: root
                    function onIconColorChanged() { icon.requestPaint() }
                    function onActiveProfileChanged() { icon.requestPaint() }
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
        onClicked: if (powerProfilesModel) powerProfilesModel.cycle()
    }
}
