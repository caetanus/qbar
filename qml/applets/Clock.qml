import QtQuick
import QtCore
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "clock"
    // Fallback fill when the theme leaves #clock unstyled (used only then).
    defaultColor: "#805f7182"
    width: preferredWidth > 0 ? preferredWidth : (clockText.implicitWidth + paddingLeft + paddingRight)
    height: theme.height

    // Tooltip text: time on the first line, full date on the second.
    // toLocaleString (not Qt.formatDateTime): Qt6 formats day/month names with the
    // C locale regardless of LANG; only the Locale overloads honour it.
    property string tooltipText: new Date().toLocaleTimeString(Qt.locale(), "HH:mm:ss") + "\n"
        + new Date().toLocaleDateString(Qt.locale(), "dddd, d MMMM yyyy")
    property bool tooltipHovered: false

    // The engine (CssTheme::loadCss) PUSHES the resolved #clock rules into CssRect's
    // `style` sink. Expose it under the name the rest of this applet reads.
    readonly property var cssStyle: root.style
    readonly property real paddingTop: cssLengthFromList("padding", 0, 0)
    readonly property real paddingRight: cssLengthFromList("padding", 1, 10)
    readonly property real paddingBottom: cssLengthFromList("padding", 2, 0)
    readonly property real paddingLeft: cssLengthFromList("padding", 3, 10)

    Behavior on width {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    signal activated()
    signal preferredWidthUpdated(int width)

    property int preferredWidth: 0

    // Reachable over the JSON IPC as the "clock" popup (e.g. a keyboard shortcut):
    // reuse the click path's activated() so the calendar opens exactly as on click.
    function open() { root.activated() }
    function toggle() { root.activated() }
    function close() { if (typeof qbarPopups !== "undefined" && qbarPopups) qbarPopups.closeAll() }

    function cssBoxParts(name) {
        var raw = cssStyle[name] || ""
        return raw.toString().trim().split(/\s+/).filter(function(part) { return part.length > 0 })
    }

    function cssLengthFromList(name, index, fallback) {
        var parts = cssBoxParts(name)
        if (parts.length === 0) {
            return fallback
        }
        var value = parts[0]
        if (parts.length === 2) {
            value = index % 2 === 0 ? parts[0] : parts[1]
        } else if (parts.length === 3) {
            value = index === 3 ? parts[1] : parts[index]
        } else if (parts.length >= 4) {
            value = parts[index]
        }
        return cssTheme.parseLength(value, fallback)
    }

    Settings {
        id: clockSettings
        category: "Clock"
        property int formatIndex: 0
    }

    property int formatIndex: 0
    property var formats: [
        "HH:mm:ss",
        "ddd HH:mm",
        "MMM ddd HH:mm",
        "HH:mm, dddd, d MMM yy"
    ]

    function currentFormat() {
        return formats[formatIndex]
    }

    function syncPreferredWidth() {
        preferredWidth = clockText.implicitWidth + paddingLeft + paddingRight
        preferredWidthUpdated(preferredWidth)
    }

    function setFormatIndex(index) {
        var normalized = (index + formats.length) % formats.length
        if (formatIndex === normalized) {
            return
        }
        formatIndex = normalized
        clockText.text = new Date().toLocaleString(Qt.locale(), root.currentFormat())
        syncPreferredWidth()
    }

    Component.onCompleted: {
        formatIndex = clockSettings.formatIndex
        clockText.text = new Date().toLocaleString(Qt.locale(), root.currentFormat())
        syncPreferredWidth()
        if (typeof qbarIpc !== "undefined" && qbarIpc)
            qbarIpc.registerPopup("clock", root)
    }

    onFormatIndexChanged: {
        clockSettings.formatIndex = formatIndex
    }

    onPaddingLeftChanged: syncPreferredWidth()
    onPaddingRightChanged: syncPreferredWidth()

    // Background is painted by the CssQmlItem base (Shape-backed CssFill); no explicit
    // fill here. Content below sits in the base's content slot, above that background.

    // CssText resolves #clock colour / font / text-shadow itself (self-registers with the
    // engine); it self-styles via the pushed `style`, so no manual colour/font here.
    QBar.CssText {
        id: clockText
        cssId: "clock"
        anchors.fill: parent
        anchors.leftMargin: root.paddingLeft
        anchors.rightMargin: root.paddingRight
        anchors.topMargin: root.paddingTop
        anchors.bottomMargin: root.paddingBottom
        horizontalAlignment: Text.AlignHCenter
        text: new Date().toLocaleString(Qt.locale(), root.currentFormat())
        onImplicitWidthChanged: root.syncPreferredWidth()
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            var now = new Date()
            clockText.text = now.toLocaleString(Qt.locale(), root.currentFormat())
            root.tooltipText = now.toLocaleTimeString(Qt.locale(), "HH:mm:ss") + "\n"
                + now.toLocaleDateString(Qt.locale(), "dddd, d MMMM yyyy")
        }
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onClicked: root.activated()
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onWheel: function(wheel) {
            var delta = wheel.angleDelta.y > 0 ? 1 : -1
            root.setFormatIndex(formatIndex + delta)
        }
    }
}
