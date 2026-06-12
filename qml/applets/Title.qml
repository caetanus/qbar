import QtQuick

Item {
    id: root
    width: 380
    height: theme.height

    property real barCenterX: width / 2
    property int textPadding: 8

    signal activated()

    Rectangle {
        anchors.fill: parent
        color: "#665b6978"
    }

    Text {
        width: Math.min(root.width, implicitWidth + root.textPadding * 2)
        x: Math.max(0, Math.min(root.width - width, root.barCenterX - width / 2))
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: root.textPadding
        rightPadding: root.textPadding
        color: theme.foreground
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: i3Ipc && i3Ipc.currentWindowTitle.length > 0 ? i3Ipc.currentWindowTitle : ""
    }
}
