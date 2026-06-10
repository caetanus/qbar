import QtQuick

Row {
    id: root
    width: 72
    height: theme.height
    spacing: 4

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#805f7182"
        z: -1
    }

    Repeater {
        model: 3

        Rectangle {
            width: 8
            height: 8
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: "#55ffffff"
        }
    }
}
