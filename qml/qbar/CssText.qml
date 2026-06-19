import QtQuick
import "qrc:/qbar" as QBar

// CssText IS a Text — a CSS-styled label. It carries the CssQmlItem signature; the engine
// pushes the resolved rules into `style` (the reverse slot — reload- and cssClass-aware),
// and the label binds its colour / font / text-shadow to that pushed `style`. Used without
// a cssId it is just a plain Text. Override `color`/`font` to opt a specific label out.
Text {
    id: root

    // --- CssQmlItem signature (read off this object by CssTheme::loadCss) ---
    property string cssId: ""
    property var cssAlternateId: []
    property var cssClass: []
    property string cssPrimitive: "text"
    property string cssPart: ""
    // Engine writes the resolved rules here; the bindings below key off it.
    property var style: ({})

    // Fallbacks when the theme leaves the label unstyled.
    property color defaultColor: theme.foreground
    property string defaultFontFamily: theme.fontFamily

    color: (style && style["color"]) ? cssTheme.parseColor(style["color"]) : root.defaultColor
    font.family: (style && style["font-family"]) ? style["font-family"] : root.defaultFontFamily
    font.pointSize: (style && style["font-size"]) ? cssTheme.parseLength(style["font-size"], theme.fontSize) : theme.fontSize
    verticalAlignment: Text.AlignVCenter

    // CSS `text-shadow` → drop-shadow layer.
    readonly property var _dropShadow: (cssTheme && cssTheme.loaded && style && style["text-shadow"])
        ? cssTheme.parseBoxShadow(style["text-shadow"]) : ({})
    layer.enabled: root._dropShadow.color !== undefined
    layer.effect: QBar.CssDropShadow { shadow: root._dropShadow }

    Component.onCompleted: {
        if (cssTheme && root.cssId.length > 0)
            cssTheme.loadCss(root)
    }
}
