import QtQuick

Item {
    id: root
    width: 72
    implicitWidth: 72
    implicitHeight: 28
    height: implicitHeight

    property int usage: 0

    Rectangle {
        anchors.fill: parent
        color: "#24303a"
        border.color: Qt.rgba(255, 255, 255, 0.15)
        border.width: 1
        radius: 3
    }

    Text {
        anchors.centerIn: parent
        color: "#ffffff"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
        text: usage + "%"
    }
}
