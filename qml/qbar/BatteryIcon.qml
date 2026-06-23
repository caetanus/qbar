import QtQuick

// Reusable battery glyph. The body is just walls + bottom; the whole top is the cap — a
// slight centered protrusion drawn as part of the same single outline (no filled block).
// Inside the outline, a level-proportional fill grows from the bottom, plus an optional
// charging bolt — all in `color`. Vertical, 12×18 by default; scale via width/height.
Item {
    id: root

    property int level: 0          // 0–100
    property bool charging: false
    property color color: "#ffffff"
    property real fillOpacity: 0.62
    property real outlineOpacity: 0.95
    property real outlineWidth: 1.2

    implicitWidth: 12
    implicitHeight: 18
    width: implicitWidth
    height: implicitHeight

    // Geometry shared by the outline canvas and the level fill.
    readonly property real sw: outlineWidth
    readonly property real capProtrude: Math.max(1.5, Math.round(height * 0.07)) // slight bump
    readonly property real bodyTop: sw / 2 + capProtrude   // where the body's top edge sits
    readonly property real fillPad: sw + 1                 // inset of the fill from the outline

    // Single stroked outline: U-shaped body (walls + bottom) closed at the top by the cap,
    // which is a small centered protrusion. Outline only — the cap is not a filled nub.
    Canvas {
        id: outline
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = root.color
            ctx.lineWidth = root.sw
            ctx.lineJoin = "round"
            ctx.globalAlpha = root.outlineOpacity

            var inset = root.sw / 2
            var L = inset, R = width - inset, B = height - inset
            var T = root.bodyTop, capTop = inset
            var cx = width / 2
            var capHalf = (width * 0.46) / 2
            var capL = cx - capHalf, capR = cx + capHalf
            var r = Math.min(2, (R - L) / 2, (B - T) / 2)

            ctx.beginPath()
            ctx.moveTo(L, T + r)
            ctx.lineTo(L, B - r)
            ctx.arcTo(L, B, L + r, B, r)        // bottom-left
            ctx.lineTo(R - r, B)
            ctx.arcTo(R, B, R, B - r, r)        // bottom-right
            ctx.lineTo(R, T + r)
            ctx.arcTo(R, T, R - r, T, r)        // top-right
            ctx.lineTo(capR, T)
            ctx.lineTo(capR, capTop)            // cap right wall (protrusion up)
            ctx.lineTo(capL, capTop)            // cap top
            ctx.lineTo(capL, T)                 // cap left wall (back down)
            ctx.lineTo(L + r, T)
            ctx.arcTo(L, T, L, T + r, r)        // top-left
            ctx.closePath()
            ctx.stroke()
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onColorChanged() { outline.requestPaint() }
            function onOutlineWidthChanged() { outline.requestPaint() }
            function onOutlineOpacityChanged() { outline.requestPaint() }
        }
    }

    // Level fill — inside the outline, growing from the bottom.
    Rectangle {
        x: root.fillPad
        width: Math.max(0, root.width - 2 * root.fillPad)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.fillPad
        height: {
            var avail = (root.height - root.fillPad) - (root.bodyTop + root.fillPad)
            return Math.max(0, avail * Math.max(0, Math.min(100, root.level)) / 100)
        }
        radius: 1
        color: root.color
        opacity: root.fillOpacity
        Behavior on height {
            NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
        }
    }

    // Charging bolt.
    Canvas {
        anchors.centerIn: parent
        width: 8
        height: 12
        visible: root.charging
        property color boltColor: root.color
        onVisibleChanged: if (visible) requestPaint()
        onBoltColorChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = boltColor
            ctx.beginPath()
            ctx.moveTo(4, 0)
            ctx.lineTo(1, 6)
            ctx.lineTo(4, 6)
            ctx.lineTo(2, 12)
            ctx.lineTo(7, 5)
            ctx.lineTo(4, 5)
            ctx.closePath()
            ctx.fill()
        }
    }
}
