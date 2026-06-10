import QtQuick

Item {
    id: root
    width: 380
    height: theme.height

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#665b6978"
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        leftPadding: 8
        rightPadding: 8
        color: theme.foreground
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: "Tilix: qbar"
    }
}
