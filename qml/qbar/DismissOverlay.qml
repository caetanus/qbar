import QtQuick

Item {
    id: root

    signal dismissed()

    Rectangle {
        anchors.fill: parent
        color: "#01000000"
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons

        onPressed: function(mouse) {
            mouse.accepted = true
            root.dismissed()
        }

        onWheel: function(wheel) {
            wheel.accepted = true
            root.dismissed()
        }
    }
}
