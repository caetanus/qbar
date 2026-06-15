import QtQuick
import Qt.labs.settings

Item {
    id: root
    width: preferredWidth > 0 ? preferredWidth : (clockText.implicitWidth + 20)
    height: theme.height

    readonly property string cssId: "clock"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    Behavior on width {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    signal activated()
    signal preferredWidthUpdated(int width)

    property int preferredWidth: 0

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
        preferredWidth = clockText.implicitWidth + 20
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

    Rectangle {
        anchors.fill: parent
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#805f7182"
    }

    Text {
        id: clockText
        anchors.centerIn: parent
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
        onTriggered: clockText.text = Qt.formatDateTime(new Date(), root.currentFormat())
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onClicked: root.activated()
        onWheel: function(wheel) {
            var delta = wheel.angleDelta.y > 0 ? 1 : -1
            root.setFormatIndex(formatIndex + delta)
        }
    }
}
