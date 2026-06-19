import QtQuick
import "qrc:/qbar" as QBar

// Thin compatibility shim over CssRect (the Shape-based fill renderer). A Shape cannot
// fill with an image, so CssFill adds the `background-image` layer BEHIND a CssRect;
// everything else — solid colour, linear-gradient, border, box-shadow, inset bevel and
// the CSS `transition` fade — is CssRect. New code should use CssRect directly; CssFill
// remains for container/popup callers that use url() image backgrounds.
Item {
    id: root

    property var style: ({})
    property real radius: 0
    property color defaultColor: "transparent"
    property color defaultBorderColor: "transparent"
    property real defaultBorderWidth: 0
    // Deprecated: the transition is derived from `style` by CssRect now. Kept so existing
    // callers that still assign these don't error — they have no effect.
    property int transitionMs: 0
    property int transitionEasingType: 0

    readonly property string backgroundValue: (style && style["background"]) ? style["background"] : ""
    readonly property string imageValue: (style && style["background-image"]) ? style["background-image"]
        : (root.isCssUrl(backgroundValue) ? backgroundValue : "")
    readonly property bool bgIsImage: root.isCssUrl(imageValue)
    readonly property string bgImageSource: root.imageSource(imageValue)
    readonly property color solidColor: (style && style["background-color"])
        ? cssTheme.parseColor(style["background-color"]) : root.defaultColor

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

    // Image background sits BEHIND the CssRect fill.
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

    // The fill / border / gradient / box-shadow / bevel / transition — all CssRect. When
    // an image is present the fill is transparent (CssRect renders url() bgs transparent),
    // so the image above shows through and the border still frames it.
    QBar.CssRect {
        anchors.fill: parent
        style: root.style
        radius: root.radius
        defaultColor: root.bgIsImage ? "transparent" : root.defaultColor
        defaultBorderColor: root.defaultBorderColor
        defaultBorderWidth: root.defaultBorderWidth
    }
}
