import QtQuick

Item {
    id: root
    width: 78
    height: theme.height

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#f0c808"
    }

    Text {
        anchors.centerIn: parent
        color: "#171717"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: "22% ♫"
    }
}
