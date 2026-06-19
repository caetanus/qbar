import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// CssRect — a CSS-styled rectangle whose fill is rendered by a Shape (a Rectangle is just
// a closed rounded-rect path, so the fill gets the full Shape capability: solid colour,
// linear-gradient, border, box-shadow, inset bevel + the alpha-fix + CSS `transition`).
// It is the ONE fill renderer in qbar (CssFill is a thin shim that only adds the
// background-image layer a Shape cannot fill).
//
// NOTE the root is an `Item`, not the `Shape` itself: the fill's alpha is applied via the
// Shape's `opacity` (a translucent Shape fill blends wrong — render opaque, alpha via
// opacity), and `opacity` is INHERITED by children. If the Shape were the root, a
// translucent fill would dim/hide the element's CONTENT. So the Shape fill is an inner
// child and content/bevel are its SIBLINGS — the fill alpha never touches them.
//
// It also carries the CssQmlItem signature: an element sets its identity (cssId, optional
// waybar alias cssAlternateId, state cssClass, cssPrimitive, cssPart) and the engine
// PUSHES the resolved rules into `style` via cssTheme.loadCss(this) — registered on
// completion, re-applied on theme reload and cssClass change (the reverse slot). Used as a
// plain renderer (explicit `style`, no cssId), it simply skips registration.
Item {
    id: root

    // --- CssQmlItem signature (read off this object by CssTheme::loadCss) ---
    property string cssId: ""
    property var cssAlternateId: []
    property var cssClass: []
    property string cssPrimitive: "rect"
    // A named part of a module (`#tray.item`, `#cpu.graph`): when set, the engine resolves
    // ONLY that part (resolvePart, excluding the bare `#id` base) so a sub-element doesn't
    // inherit the container's own background.
    property string cssPart: ""
    // Engine writes the resolved rules here; everything below keys off it.
    property var style: ({})

    property real radius: 0
    property color defaultColor: "transparent"
    property color defaultBorderColor: "transparent"
    property real defaultBorderWidth: 0

    // Standard CSS `transition` (shorthand) → synced colour+opacity fade. The opaque fill
    // COLOUR and the fill Shape's OPACITY animate with the same duration/easing.
    readonly property var _transition: (cssTheme && cssTheme.loaded && root.style && root.style["transition"])
        ? cssTheme.parseTransition(root.style["transition"]) : ({})
    readonly property int transitionMs: root._transition.duration !== undefined ? root._transition.duration : 0
    readonly property int transitionEasingType: root._transition.easing !== undefined ? root._transition.easing : Easing.InOutQuad

    readonly property string backgroundValue: (style && style["background"]) ? style["background"] : ""
    // A url() background is an image (handled by the CssFill shim, not by a Shape fill);
    // exclude it here so it isn't parsed as a colour.
    readonly property string bgValue: root.isCssUrl(backgroundValue) ? "" : backgroundValue
    readonly property bool bgIsGradient: bgValue.indexOf("gradient") >= 0
    readonly property var gradient: (cssTheme && cssTheme.loaded && bgIsGradient)
        ? cssTheme.parseGradient(bgValue) : ({})
    readonly property bool hasGradient: gradient && gradient.stops !== undefined
    // Peak alpha across the gradient stops — the gradient is drawn with OPAQUE stops and its
    // alpha applied via the fill Shape's `opacity` (translucent stops blend wrong).
    readonly property real gradientPeakAlpha: {
        if (!hasGradient || !gradient.stops)
            return 1.0
        var m = 0.0
        for (var i = 0; i < gradient.stops.length; ++i)
            m = Math.max(m, gradient.stops[i].color.a)
        return m
    }

    readonly property color solidColor: (style && style["background-color"])
        ? cssTheme.parseColor(style["background-color"])
        : ((bgValue && !bgIsGradient) ? cssTheme.parseColor(bgValue) : root.defaultColor)

    readonly property color borderColor: (style && style["border-color"])
        ? cssTheme.parseColor(style["border-color"]) : root.defaultBorderColor
    readonly property real borderWidth: (style && style["border-width"])
        ? parseFloat(style["border-width"]) : root.defaultBorderWidth

    readonly property var shadowList: (cssTheme && cssTheme.loaded && style && style["box-shadow"])
        ? cssTheme.parseBoxShadowList(style["box-shadow"]) : []
    readonly property var shadow: shadowList.length > 0 ? shadowList[0] : ({})
    readonly property bool hasShadow: shadow && shadow.color !== undefined
    // `box-shadow: inset ...` → recessed bevel: dark inner edge top/left, light bottom/right.
    readonly property var insetShadows: root.insetShadowsOf(root.shadowList)
    readonly property bool insetBevel: root.insetShadows.length > 0
    readonly property color insetDarkColor: root.insetShadows.length > 0
        ? root.insetShadows[0].color : "transparent"
    readonly property color insetLightColor: root.insetShadows.length > 1
        ? root.insetShadows[1].color : Qt.rgba(1, 1, 1, 0.30)
    readonly property var borderRadii: root.parseBorderRadius(style && style["border-radius"] ? style["border-radius"] : "")

    // CSS gradient line: angle 0deg points up, increasing clockwise.
    readonly property real angleRad: ((hasGradient ? gradient.angle : 180) * Math.PI) / 180
    readonly property real dirX: Math.sin(angleRad)
    readonly property real dirY: -Math.cos(angleRad)
    readonly property real lineLen: Math.abs(width * dirX) + Math.abs(height * dirY)

    function insetShadowsOf(list) {
        var out = []
        for (var i = 0; i < list.length; ++i) {
            if (list[i] && list[i].inset === true)
                out.push(list[i])
        }
        return out
    }

    function isCssUrl(value) {
        return value && value.trim().toLowerCase().indexOf("url(") === 0
    }

    function parseCssLength(value, fallback) {
        var n = parseFloat(value)
        return isNaN(n) ? fallback : n
    }

    function parseBorderRadius(value) {
        if (!value || String(value).trim().length === 0)
            return [root.radius, root.radius, root.radius, root.radius]

        // Elliptical radii ("a / b") accepted syntactically; the horizontal side is used
        // because ShapePath corners are circular.
        var s = String(value).split("/")[0].trim()
        var parts = s.split(/\s+/)
        var values = []
        for (var i = 0; i < parts.length && i < 4; ++i)
            values.push(root.parseCssLength(parts[i], root.radius))
        if (values.length === 0)
            return [root.radius, root.radius, root.radius, root.radius]
        if (values.length === 1)
            return [values[0], values[0], values[0], values[0]]
        if (values.length === 2)
            return [values[0], values[1], values[0], values[1]]
        if (values.length === 3)
            return [values[0], values[1], values[2], values[1]]
        return [values[0], values[1], values[2], values[3]]
    }

    function clampedRadius(index) {
        return Math.max(0, Math.min(root.borderRadii[index], root.width / 2, root.height / 2))
    }

    readonly property bool partiallyRounded: root.borderRadii[0] > 0 || root.borderRadii[1] > 0
        || root.borderRadii[2] > 0 || root.borderRadii[3] > 0

    // A fully-square edge of a partially-rounded box butts against a neighbour — skip its
    // inset bevel so the two read as one recessed unit, not two cells split by a line.
    function bevelEdge(cornerA, cornerB) {
        return !(root.partiallyRounded && root.borderRadii[cornerA] <= 0 && root.borderRadii[cornerB] <= 0)
    }

    // The fill. Its alpha rides on the Shape's `opacity` (a translucent Shape fill blends
    // wrong); because this Shape is NOT the root, that opacity dims only the fill, never the
    // content/bevel siblings below.
    Shape {
        id: fill
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        opacity: root.hasGradient ? root.gradientPeakAlpha : root.solidColor.a

        Behavior on opacity {
            enabled: root.transitionMs > 0
            NumberAnimation {
                duration: root.transitionMs
                easing.type: root.transitionEasingType
            }
        }

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
            fillColor: root.hasGradient
                ? "transparent"
                : Qt.rgba(root.solidColor.r, root.solidColor.g, root.solidColor.b, 1.0)
            fillGradient: root.hasGradient ? linearGradient : null

            Behavior on fillColor {
                enabled: root.transitionMs > 0
                ColorAnimation {
                    duration: root.transitionMs
                    easing.type: root.transitionEasingType
                }
            }

            startX: root.clampedRadius(0)
            startY: 0

            PathLine { x: Math.max(root.clampedRadius(0), root.width - root.clampedRadius(1)); y: 0 }
            PathQuad {
                x: root.width
                y: root.clampedRadius(1)
                controlX: root.width
                controlY: 0
            }
            PathLine { x: root.width; y: Math.max(root.clampedRadius(1), root.height - root.clampedRadius(2)) }
            PathQuad {
                x: root.width - root.clampedRadius(2)
                y: root.height
                controlX: root.width
                controlY: root.height
            }
            PathLine { x: root.clampedRadius(3); y: root.height }
            PathQuad {
                x: 0
                y: root.height - root.clampedRadius(3)
                controlX: 0
                controlY: root.height
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

    // Sunken bevel for `box-shadow: inset`. Sibling of the fill (so the fill's alpha does
    // not dim it). Thin edge lines inset past the rounded corners.
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

    // Applet content sits above the fill — a SIBLING of the fill Shape, so the fill's
    // alpha-opacity never dims it.
    default property alias content: contentHolder.data
    Item { id: contentHolder; anchors.fill: parent }

    Component { id: stopComponent; GradientStop {} }

    function rebuildStops() {
        var built = []
        // Read `gradient` DIRECTLY — NOT via the derived `hasGradient` binding. When
        // `gradient` changes, onGradientChanged runs this BEFORE the separate `hasGradient`
        // binding recomputes, so `hasGradient` is stale (false) here even though `gradient`
        // already has stops — gating on it builds 0 stops and the Shape paints black.
        var g = root.gradient
        var list = (g && g.stops !== undefined) ? g.stops : []
        for (var i = 0; i < list.length; ++i) {
            // Opaque stop; the gradient's alpha is the fill Shape's `opacity` (gradientPeakAlpha).
            var c = list[i].color
            built.push(stopComponent.createObject(linearGradient,
                { position: list[i].position, color: Qt.rgba(c.r, c.g, c.b, 1.0) }))
        }
        linearGradient.stops = built
    }

    onGradientChanged: rebuildStops()

    Component.onCompleted: {
        rebuildStops()
        // Register with the engine only when this is an identified element (not a plain
        // renderer the CssFill shim drives with an explicit style).
        if (cssTheme && root.cssId.length > 0)
            cssTheme.loadCss(root)
    }

    // A dynamic cssId (e.g. CustomTool's id derived from a toolId set after creation) must
    // re-register so the engine resolves and pushes the new rules.
    onCssIdChanged: {
        if (cssTheme && root.cssId.length > 0)
            cssTheme.loadCss(root)
    }
}
