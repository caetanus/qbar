import QtQuick

Item {
    id: root
    width: label.implicitWidth + 16
    height: theme.height

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#f28c38"
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: "#ffffff"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: Qt.formatDate(new Date(), "dd°C H2")
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
