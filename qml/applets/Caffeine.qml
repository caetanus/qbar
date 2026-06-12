import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    width: 32
    height: theme.height
    property bool active: caffeineModel ? caffeineModel.active : false

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.active ? "disable screen lock" : "enable screen lock"
        side: "auto"
    }

    Rectangle {
        anchors.fill: parent
        color: root.active ? "#ffffff" : "#000000"
    }

    Image {
        id: cupIcon
        anchors.centerIn: parent
        width: 23
        height: 23
        source: root.active
            ? "qrc:/icons/caffeine-cup-full-black.svg"
            : "qrc:/icons/caffeine-cup-empty-white.svg"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (caffeineModel) {
                caffeineModel.toggle()
            }
        }
    }
}
