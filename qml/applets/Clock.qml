import QtQuick

Item {
    id: root
    width: clockText.implicitWidth + 20
    height: theme.height

    signal activated()

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
        text: Qt.formatDateTime(new Date(), "HH:mm")
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: clockText.text = Qt.formatDateTime(new Date(), "HH:mm")
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
