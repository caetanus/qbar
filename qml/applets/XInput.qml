import QtQuick

Item {
    id: root
    width: 36
    height: theme.height

    signal activated()

    property string layoutName: i3Ipc ? i3Ipc.currentKeyboardLayout : ""
    property string shortLayout: {
        if (layoutName.length === 0) {
            return "--"
        }

        var clean = layoutName
        var paren = clean.indexOf("(")
        if (paren >= 0) {
            clean = clean.slice(0, paren)
        }
        clean = clean.trim()
        return clean.slice(0, 2).toLowerCase()
    }

    Rectangle {
        anchors.fill: parent
        color: "#16c79a"
    }

    Text {
        anchors.centerIn: parent
        color: "#101418"
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: root.shortLayout
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
