import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// Renders a CSS background (solid color, linear-gradient or url() image) with
// optional rounded radius, border and box-shadow, from a resolved CssTheme style
// map. Drop it in place of a plain background Rectangle.
Item {
    id: root

    property var style: ({})
    property real radius: 0
    property color defaultColor: "transparent"
    property color defaultBorderColor: "transparent"
    property real defaultBorderWidth: 0

    readonly property string backgroundValue: (style && style["background"]) ? style["background"] : ""
    readonly property string imageValue: (style && style["background-image"]) ? style["background-image"]
        : (root.isCssUrl(backgroundValue) ? backgroundValue : "")
    readonly property string bgValue: root.isCssUrl(backgroundValue) ? "" : backgroundValue
    readonly property bool bgIsGradient: bgValue.indexOf("gradient") >= 0
    readonly property bool bgIsImage: root.isCssUrl(imageValue)
    readonly property string bgImageSource: root.imageSource(imageValue)
    readonly property var gradient: (cssTheme && cssTheme.loaded && bgIsGradient)
        ? cssTheme.parseGradient(bgValue) : ({})
    readonly property bool hasGradient: gradient && gradient.stops !== undefined

    readonly property color solidColor: (style && style["background-color"])
        ? cssTheme.parseColor(style["background-color"])
        : ((bgValue && !bgIsGradient && !root.isCssUrl(bgValue)) ? cssTheme.parseColor(bgValue) : root.defaultColor)

    readonly property color borderColor: (style && style["border-color"])
        ? cssTheme.parseColor(style["border-color"]) : root.defaultBorderColor
    readonly property real borderWidth: (style && style["border-width"])
        ? parseFloat(style["border-width"]) : root.defaultBorderWidth

    readonly property var shadow: (cssTheme && cssTheme.loaded && style && style["box-shadow"])
        ? cssTheme.parseBoxShadow(style["box-shadow"]) : ({})
    readonly property bool hasShadow: shadow && shadow.color !== undefined
    // `box-shadow: inset ...` renders a recessed (sunken) bevel instead of an
    // outer drop shadow: dark inner edge on top/left, light on bottom/right.
    readonly property bool insetBevel: root.hasShadow && root.shadow.inset === true
    readonly property color insetDarkColor: root.insetBevel ? root.shadow.color : "transparent"
    readonly property color insetLightColor: (root.style && root.style["inset-highlight"])
        ? cssTheme.parseColor(root.style["inset-highlight"]) : Qt.rgba(1, 1, 1, 0.30)
    readonly property var borderRadii: root.parseBorderRadius(style && style["border-radius"] ? style["border-radius"] : "")

    // CSS gradient line: angle 0deg points up, increasing clockwise. The line
    // runs through the center; its length is the box projected onto the angle.
    readonly property real angleRad: ((hasGradient ? gradient.angle : 180) * Math.PI) / 180
    readonly property real dirX: Math.sin(angleRad)
    readonly property real dirY: -Math.cos(angleRad)
    readonly property real lineLen: Math.abs(width * dirX) + Math.abs(height * dirY)

    function isCssUrl(value) {
        return value && value.trim().toLowerCase().indexOf("url(") === 0
    }

    function imageSource(value) {
        if (!isCssUrl(value)) {
            return ""
        }
        var s = value.trim()
        s = s.substring(4, s.length - 1).trim()
        if ((s[0] === "\"" && s[s.length - 1] === "\"") || (s[0] === "'" && s[s.length - 1] === "'")) {
            s = s.substring(1, s.length - 1)
        }
        if (s.length === 0) {
            return ""
        }
        if (s.indexOf("qrc:/") === 0 || s.indexOf("file:/") === 0 || s.indexOf("http://") === 0 || s.indexOf("https://") === 0) {
            return s
        }
        if (s[0] === "/") {
            return "file://" + s
        }
        return s
    }

    function fillModeFor(size) {
        var s = (size || "cover").toLowerCase()
        if (s === "contain") {
            return Image.PreserveAspectFit
        }
        if (s === "stretch" || s === "100% 100%") {
            return Image.Stretch
        }
        return Image.PreserveAspectCrop
    }

    function parseCssLength(value, fallback) {
        var n = parseFloat(value)
        return isNaN(n) ? fallback : n
    }

    function parseBorderRadius(value) {
        if (!value || String(value).trim().length === 0) {
            return [root.radius, root.radius, root.radius, root.radius]
        }

        // Elliptical radii ("a / b") are accepted syntactically; qbar uses the
        // horizontal side for now because ShapePath corners are circular.
        var s = String(value).split("/")[0].trim()
        var parts = s.split(/\s+/)
        var values = []
        for (var i = 0; i < parts.length && i < 4; ++i) {
            values.push(root.parseCssLength(parts[i], root.radius))
        }
        if (values.length === 0) {
            return [root.radius, root.radius, root.radius, root.radius]
        }
        if (values.length === 1) {
            return [values[0], values[0], values[0], values[0]]
        }
        if (values.length === 2) {
            return [values[0], values[1], values[0], values[1]]
        }
        if (values.length === 3) {
            return [values[0], values[1], values[2], values[1]]
        }
        return [values[0], values[1], values[2], values[3]]
    }

    function clampedRadius(index) {
        return Math.max(0, Math.min(root.borderRadii[index], root.width / 2, root.height / 2))
    }

    readonly property bool partiallyRounded: root.borderRadii[0] > 0 || root.borderRadii[1] > 0
        || root.borderRadii[2] > 0 || root.borderRadii[3] > 0

    // A fully-square edge of a partially-rounded box is meant to butt against a
    // neighbour (e.g. Clock + Tray) — skip its inset bevel so the two read as a
    // single recessed unit instead of two cells split by a line.
    function bevelEdge(cornerA, cornerB) {
        return !(root.partiallyRounded && root.borderRadii[cornerA] <= 0 && root.borderRadii[cornerB] <= 0)
    }

    Rectangle {
        anchors.fill: parent
        visible: root.bgIsImage
        color: root.solidColor
    }

    Image {
        anchors.fill: parent
        visible: root.bgIsImage && root.bgImageSource.length > 0
        source: root.bgImageSource
        fillMode: root.fillModeFor(root.style ? root.style["background-size"] : "")
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        smooth: true
        asynchronous: true
        cache: true
        opacity: root.style && root.style["background-image-opacity"] !== undefined
            ? parseFloat(root.style["background-image-opacity"]) : 1.0
    }

    Shape {
        id: shape
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        layer.enabled: root.hasShadow && !root.insetBevel
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
            fillColor: root.hasGradient || root.bgIsImage ? "transparent" : root.solidColor
            fillGradient: root.hasGradient && !root.bgIsImage ? linearGradient : null

            startX: root.clampedRadius(0)
            startY: 0

            PathLine { x: Math.max(root.clampedRadius(0), shape.width - root.clampedRadius(1)); y: 0 }
            PathQuad {
                x: shape.width
                y: root.clampedRadius(1)
                controlX: shape.width
                controlY: 0
            }
            PathLine { x: shape.width; y: Math.max(root.clampedRadius(1), shape.height - root.clampedRadius(2)) }
            PathQuad {
                x: shape.width - root.clampedRadius(2)
                y: shape.height
                controlX: shape.width
                controlY: shape.height
            }
            PathLine { x: root.clampedRadius(3); y: shape.height }
            PathQuad {
                x: 0
                y: shape.height - root.clampedRadius(3)
                controlX: 0
                controlY: shape.height
            }
            PathLine { x: 0; y: root.clampedRadius(0) }
            PathQuad {
                x: root.clampedRadius(0)
                y: 0
                controlX: 0
                controlY: 0
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

    // Sunken bevel for `box-shadow: inset`. Thin edge lines, inset past the
    // rounded corners so they sit on the straight portions of each side.
    Item {
        anchors.fill: parent
        visible: root.insetBevel

        Rectangle { // top — shadow
            visible: root.bevelEdge(0, 1)
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: root.clampedRadius(0); anchors.rightMargin: root.clampedRadius(1)
            height: 1; color: root.insetDarkColor
        }
        Rectangle { // left — shadow
            visible: root.bevelEdge(0, 3)
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.topMargin: root.clampedRadius(0); anchors.bottomMargin: root.clampedRadius(3)
            width: 1; color: root.insetDarkColor
        }
        Rectangle { // bottom — highlight
            visible: root.bevelEdge(3, 2)
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: root.clampedRadius(3); anchors.rightMargin: root.clampedRadius(2)
            height: 1; color: root.insetLightColor
        }
        Rectangle { // right — highlight
            visible: root.bevelEdge(1, 2)
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.topMargin: root.clampedRadius(1); anchors.bottomMargin: root.clampedRadius(2)
            width: 1; color: root.insetLightColor
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
