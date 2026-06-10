import QtQuick

Row {
    id: root
    width: 185
    height: theme.height
    spacing: 0

    signal activated()

    Repeater {
        model: 5

        Rectangle {
            width: label.implicitWidth + 14
            height: theme.height
            radius: 0
            color: ["#273847", "#526171", "#ff5555", "#35495c", "#4a596a"][index]

            Text {
                id: label
                anchors.centerIn: parent
                color: "#eef2f7"
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                text: index === 4 ? "5:●" : (index + 1) + ":>"
            }
        }
    }
}
