import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// Renders a CSS background (solid color or linear-gradient at any angle) with an
// optional rounded radius, border and box-shadow, from a resolved CssTheme style
// map. Drop it in place of a plain background Rectangle.
Item {
    id: root

    property var style: ({})
    property real radius: 0
    property color defaultColor: "transparent"
    property color defaultBorderColor: "transparent"
    property real defaultBorderWidth: 0

    // "background" / "background-image" may carry a gradient; otherwise a color.
    readonly property string bgValue: (style && style["background"]) ? style["background"]
        : ((style && style["background-image"]) ? style["background-image"] : "")
    readonly property bool bgIsGradient: bgValue.indexOf("gradient") >= 0
    readonly property var gradient: (cssTheme && cssTheme.loaded && bgIsGradient)
        ? cssTheme.parseGradient(bgValue) : ({})
    readonly property bool hasGradient: gradient && gradient.stops !== undefined

    readonly property color solidColor: (style && style["background-color"])
        ? cssTheme.parseColor(style["background-color"])
        : ((bgValue && !bgIsGradient) ? cssTheme.parseColor(bgValue) : root.defaultColor)

    readonly property color borderColor: (style && style["border-color"])
        ? cssTheme.parseColor(style["border-color"]) : root.defaultBorderColor
    readonly property real borderWidth: (style && style["border-width"])
        ? parseFloat(style["border-width"]) : root.defaultBorderWidth

    readonly property var shadow: (cssTheme && cssTheme.loaded && style && style["box-shadow"])
        ? cssTheme.parseBoxShadow(style["box-shadow"]) : ({})
    readonly property bool hasShadow: shadow && shadow.color !== undefined

    // CSS gradient line: angle 0deg points up, increasing clockwise. The line
    // runs through the center; its length is the box projected onto the angle.
    readonly property real angleRad: ((hasGradient ? gradient.angle : 180) * Math.PI) / 180
    readonly property real dirX: Math.sin(angleRad)
    readonly property real dirY: -Math.cos(angleRad)
    readonly property real lineLen: Math.abs(width * dirX) + Math.abs(height * dirY)

    Shape {
        id: shape
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        layer.enabled: root.hasShadow
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: root.hasShadow ? root.shadow.color : "transparent"
            shadowHorizontalOffset: root.hasShadow ? root.shadow.x : 0
            shadowVerticalOffset: root.hasShadow ? root.shadow.y : 0
            shadowBlur: root.hasShadow ? Math.min(1, root.shadow.blur / 32) : 0
            autoPaddingEnabled: true
        }

        ShapePath {
            strokeColor: root.borderColor
            strokeWidth: root.borderWidth
            fillColor: root.hasGradient ? "transparent" : root.solidColor
            fillGradient: root.hasGradient ? linearGradient : null

            PathRectangle {
                width: shape.width
                height: shape.height
                radius: root.radius
            }
        }

        LinearGradient {
            id: linearGradient
            x1: root.width / 2 - root.dirX * root.lineLen / 2
            y1: root.height / 2 - root.dirY * root.lineLen / 2
            x2: root.width / 2 + root.dirX * root.lineLen / 2
            y2: root.height / 2 + root.dirY * root.lineLen / 2
        }
    }

    Component { id: stopComponent; GradientStop {} }

    function rebuildStops() {
        var built = []
        if (hasGradient) {
            var list = gradient.stops
            for (var i = 0; i < list.length; ++i) {
                built.push(stopComponent.createObject(linearGradient,
                    { position: list[i].position, color: list[i].color }))
            }
        }
        linearGradient.stops = built
    }

    onGradientChanged: rebuildStops()
    Component.onCompleted: rebuildStops()
}
