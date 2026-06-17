import QtQuick
import Qt.labs.settings
import "qrc:/qbar" as QBar

Item {
    id: root
    width: preferredWidth > 0 ? preferredWidth : (clockText.implicitWidth + paddingLeft + paddingRight)
    height: theme.height

    // Tooltip text: time on the first line, full date on the second.
    property string tooltipText: Qt.formatDateTime(new Date(), "HH:mm:ss") + "\n"
        + Qt.formatDateTime(new Date(), "dddd, d MMMM yyyy")
    property bool tooltipHovered: false

    readonly property string cssId: "clock"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})
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
        clockText.text = Qt.formatDateTime(new Date(), root.currentFormat())
        syncPreferredWidth()
    }

    Component.onCompleted: {
        formatIndex = clockSettings.formatIndex
        clockText.text = Qt.formatDateTime(new Date(), root.currentFormat())
        syncPreferredWidth()
    }

    onFormatIndexChanged: {
        clockSettings.formatIndex = formatIndex
    }

    onPaddingLeftChanged: syncPreferredWidth()
    onPaddingRightChanged: syncPreferredWidth()

    QBar.CssFill {
        anchors.fill: parent
        style: root.cssStyle
        radius: root.cssStyle["border-radius"] ? cssTheme.parseLength(root.cssStyle["border-radius"], 0) : 0
        defaultColor: root.cssStyle["background-color"] || root.cssStyle["background"] ? "transparent" : "#805f7182"
    }

    Text {
        id: clockText
        anchors.fill: parent
        anchors.leftMargin: root.paddingLeft
        anchors.rightMargin: root.paddingRight
        anchors.topMargin: root.paddingTop
        anchors.bottomMargin: root.paddingBottom
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        readonly property var dropShadow: cssTheme && cssTheme.loaded
            ? cssTheme.parseBoxShadow(root.cssStyle["text-shadow"] || "") : ({})
        layer.enabled: dropShadow.color !== undefined
        layer.effect: QBar.CssDropShadow { shadow: clockText.dropShadow }

        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        text: Qt.formatDateTime(new Date(), root.currentFormat())
        onImplicitWidthChanged: root.syncPreferredWidth()
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            var now = new Date()
            clockText.text = Qt.formatDateTime(now, root.currentFormat())
            root.tooltipText = Qt.formatDateTime(now, "HH:mm:ss") + "\n"
                + Qt.formatDateTime(now, "dddd, d MMMM yyyy")
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
