import QtQuick

Item {
    id: root
    width: clockText.implicitWidth + 20
    height: theme.height

    Behavior on width {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    signal activated()
    signal preferredWidthUpdated(int width)

    property int preferredWidth: width

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

    Rectangle {
        anchors.fill: parent
        color: "#805f7182"
    }

    Text {
        id: clockText
        anchors.centerIn: parent
        color: theme.foreground
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: Qt.formatDateTime(new Date(), root.currentFormat())
        onImplicitWidthChanged: root.preferredWidthUpdated(implicitWidth + 20)
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
            formatIndex = (formatIndex + delta + formats.length) % formats.length
            clockText.text = Qt.formatDateTime(new Date(), root.currentFormat())
        }
    }
}
