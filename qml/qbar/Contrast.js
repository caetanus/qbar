.pragma library

function luminance(c) {
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
}

function blendOver(fg, bg) {
    var a = fg.a
    return Qt.rgba(fg.r * a + bg.r * (1 - a), fg.g * a + bg.g * (1 - a), fg.b * a + bg.b * (1 - a), 1)
}

function toHex2(v) {
    var n = Math.max(0, Math.min(255, Math.round(v * 255)))
    var s = n.toString(16)
    return s.length === 1 ? "0" + s : s
}

function toHex(c) {
    return "#" + toHex2(c.r) + toHex2(c.g) + toHex2(c.b)
}

// window#waybar background-color, falling back when the CSS theme has none loaded
function barBackground(cssTheme, fallback) {
    // `fallback` is theme.background, a "#AARRGGBB" string from the QVariantMap
    // context property — parse it so .r/.g/.b/.a are usable by blendOver below.
    if (!cssTheme || !cssTheme.loaded) {
        return cssTheme ? cssTheme.parseColor(fallback) : fallback
    }
    var style = cssTheme.resolve("waybar")
    return style["background-color"] ? cssTheme.parseColor(style["background-color"]) : cssTheme.parseColor(fallback)
}

// What an element actually sits on: its own background if opaque/translucent,
// otherwise the bar's background (blended through if translucent).
function effectiveBackground(ownBg, cssTheme, fallback) {
    var bar = barBackground(cssTheme, fallback)
    return ownBg.a > 0 ? blendOver(ownBg, bar) : bar
}

// Whether icons/text on this background should use the dark contrast color
// (as opposed to white).
function needsDarkIcon(bg) {
    return luminance(bg) >= 0.5
}

// High-contrast foreground (icon/text) color for the given background. Pure
// black reads as too harsh on light themes, so it's softened to a charcoal
// gray by blending at reduced opacity over the background. Themes that want
// full #1a1a1a/#ffffff can set "color" explicitly in their CSS, which takes
// precedence over this fallback at every call site.
function contrastColor(bg) {
    if (!needsDarkIcon(bg)) {
        return "#ffffff"
    }
    return toHex(blendOver(Qt.rgba(26 / 255, 26 / 255, 26 / 255, 0.75), bg))
}

// Contrast color as an rgba() string at the given alpha, for canvas fills
function contrastFill(bg, alpha) {
    return luminance(bg) < 0.5
        ? "rgba(255, 255, 255, " + alpha + ")"
        : "rgba(26, 26, 26, " + alpha + ")"
}
