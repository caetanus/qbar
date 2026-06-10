import QtQuick

Item {
    id: root
    width: 36
    height: theme.height

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#16c79a"
    }

    Text {
        anchors.centerIn: parent
        color: "#101418"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: "US"
    }
}
