import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

// Redshift button: screen colour temperature by time of day. The icon is a sun on the
// horizon that warms towards amber as the applied temperature drops; dimmed when off or
// when no output is under control. Click toggles, wheel nudges (manual), middle-click
// returns to automatic; right-click opens a menu of temperature presets. Themes style #redshift with .active/.manual and the phase
// (.day/.night/.sunrise/.sunset) as states.
QBar.CssRect {
    id: root
    cssId: "redshift"
    cssClass: {
        var classes = []
        if (root.active) classes.push("active")
        if (root.manual) classes.push("manual")
        if (root.phase.length > 0) classes.push(root.phase)
        return classes
    }
    width: 30
    height: theme.height

    readonly property var cssStyle: root.style

    readonly property bool active: redshiftModel ? redshiftModel.enabled : false
    readonly property bool manual: redshiftModel ? redshiftModel.manual : false
    readonly property bool available: redshiftModel ? redshiftModel.available : false
    readonly property string phase: redshiftModel ? redshiftModel.phase : ""
    readonly property real warmth: redshiftModel ? redshiftModel.warmth : 0
    readonly property int temperature: redshiftModel ? redshiftModel.temperature : 6500
    readonly property int dayTemperature: redshiftModel ? redshiftModel.dayTemperature : 6500
    readonly property int nightTemperature: redshiftModel ? redshiftModel.nightTemperature : 4000
    readonly property string tooltipText: redshiftModel ? redshiftModel.tooltipText : "redshift unavailable"

    readonly property color backgroundColor: cssStyle["background-color"]
        ? cssTheme.parseColor(cssStyle["background-color"])
        : "transparent"
    // Theme colour, else contrast against whatever is behind the button.
    readonly property color baseColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : Contrast.contrastColor(Contrast.effectiveBackground(root.backgroundColor, cssTheme, theme.background))
    readonly property color warmColor: "#f5a623"
    readonly property color iconColor: root.active
        ? Qt.rgba(baseColor.r + (warmColor.r - baseColor.r) * warmth,
                  baseColor.g + (warmColor.g - baseColor.g) * warmth,
                  baseColor.b + (warmColor.b - baseColor.b) * warmth, 1)
        : baseColor
    readonly property real iconOpacity: root.active && root.available ? 1.0 : 0.45

    onIconColorChanged: icon.requestPaint()
    onIconOpacityChanged: icon.requestPaint()
    onActiveChanged: icon.requestPaint()

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.tooltipText
        side: "auto"
    }

    // Right-click menu: automatic schedule, temperature presets (manual hold), on/off.
    readonly property var presets: [
        { kelvin: 2700, label: qsTr("Candle") },
        { kelvin: 3400, label: qsTr("Very warm") },
        { kelvin: 4000, label: qsTr("Warm") },
        { kelvin: 4500, label: "" },
        { kelvin: 5000, label: qsTr("Mild") },
        { kelvin: 5500, label: "" },
        { kelvin: 6500, label: qsTr("Neutral") }
    ]

    function buildMenu() {
        var items = []
        items.push({
            text: qsTr("Automatic (%1 K night · %2 K day)").arg(root.nightTemperature).arg(root.dayTemperature),
            checkable: true,
            checked: root.active && !root.manual,
            action: "auto"
        })
        items.push({ separator: true })
        var listed = false
        for (var i = 0; i < root.presets.length; i++) {
            var p = root.presets[i]
            var label = p.label.length > 0 ? p.kelvin + " K · " + p.label : p.kelvin + " K"
            var held = root.active && root.manual && root.temperature === p.kelvin
            listed = listed || held
            items.push({ text: label, checkable: true, checked: held, action: "set", kelvin: p.kelvin })
        }
        // A wheel-nudged value between presets shows as its own checked entry.
        if (root.active && root.manual && !listed) {
            items.push({ text: root.temperature + " K", checkable: true, checked: true, action: "set", kelvin: root.temperature })
        }
        items.push({ separator: true })
        items.push({ text: root.active ? qsTr("Turn off") : qsTr("Turn on"), action: "toggle" })
        return items
    }

    QBar.MenuPopup {
        id: menu
        anchorItem: root
        gap: 4
        onTriggered: function(index, item) {
            if (!redshiftModel) {
                return
            }
            if (item.action === "auto") {
                redshiftModel.resetAuto()
                redshiftModel.enabled = true
            } else if (item.action === "set") {
                redshiftModel.setTemperature(item.kelvin)
            } else if (item.action === "toggle") {
                redshiftModel.toggle()
            }
        }
    }

    // Background painted by the CssRect base (per-state via cssClass).

    Item {
        width: 16
        height: 16
        anchors.centerIn: parent

        QBar.CssIcon {
            id: cssIcon
            anchors.fill: parent
            style: root.cssStyle
            color: root.iconColor
            opacity: root.iconOpacity
            visible: hasCustomIcon
        }

        Canvas {
            id: icon
            anchors.fill: parent
            visible: !cssIcon.hasCustomIcon
            renderTarget: Canvas.Image
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.globalAlpha = root.iconOpacity
                ctx.strokeStyle = root.iconColor
                ctx.fillStyle = root.iconColor
                ctx.lineWidth = 1.4
                ctx.lineCap = "round"

                // Sun sitting on the horizon: a half disc, three rays, the horizon line.
                var cx = 8, hy = 11, r = 3.6
                ctx.beginPath()
                ctx.arc(cx, hy, r, Math.PI, 2 * Math.PI)
                ctx.closePath()
                if (root.active) {
                    ctx.fill()
                } else {
                    ctx.stroke()
                }
                var rays = [-Math.PI / 2, -Math.PI / 2 - Math.PI / 4, -Math.PI / 2 + Math.PI / 4]
                for (var i = 0; i < rays.length; ++i) {
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(rays[i]) * (r + 1.6), hy + Math.sin(rays[i]) * (r + 1.6))
                    ctx.lineTo(cx + Math.cos(rays[i]) * (r + 3.4), hy + Math.sin(rays[i]) * (r + 3.4))
                    ctx.stroke()
                }
                ctx.beginPath()
                ctx.moveTo(1.5, hy)
                ctx.lineTo(14.5, hy)
                ctx.stroke()
            }
            onVisibleChanged: if (visible) requestPaint()
            Component.onCompleted: requestPaint()
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (!redshiftModel) {
                return
            }
            if (mouse.button === Qt.RightButton) {
                menu.model = root.buildMenu()
                menu.toggle()
            } else if (mouse.button === Qt.MiddleButton) {
                redshiftModel.resetAuto()
            } else {
                redshiftModel.toggle()
            }
        }
        onWheel: function(wheel) {
            if (!redshiftModel) {
                return
            }
            if (wheel.angleDelta.y > 0) {
                redshiftModel.nudge(250)
            } else if (wheel.angleDelta.y < 0) {
                redshiftModel.nudge(-250)
            }
        }
    }
}
