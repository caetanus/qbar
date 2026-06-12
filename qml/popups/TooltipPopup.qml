import QtQuick

Item {
    id: root
    width: implicitWidth
    height: implicitHeight
    implicitWidth: label.implicitWidth + 20
    implicitHeight: 28

    property string text: ""

    Rectangle {
        anchors.fill: parent
        color: "#24303a"
        border.color: Qt.rgba(255, 255, 255, 0.15)
        border.width: 1
        radius: 3
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: "#ffffff"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
        text: root.text
    }
}
