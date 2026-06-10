import QtQuick

Item {
    id: root
    width: 64
    height: theme.height

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#2ecc71"
    }

    Text {
        anchors.centerIn: parent
        color: "#101418"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: "12% ▮"
    }
}
