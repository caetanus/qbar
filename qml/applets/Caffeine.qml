import QtQuick

Item {
    id: root
    width: 32
    height: theme.height
    property bool active: false

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: root.active ? "#e0c349" : "#e7edf3"
    }

    Text {
        anchors.centerIn: parent
        color: "#1f2933"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize + 1
        text: "◉"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.active = !root.active
            root.activated()
        }
    }
}
