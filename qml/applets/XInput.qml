import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "keyboard"
    defaultColor: "#16c79a"
    width: 36
    height: theme.height

    readonly property var cssStyle: root.style

    signal activated()

    // When Caps Lock is on, the layout text is shown uppercased as the indicator.
    property bool capsActive: capsLock ? capsLock.active : false
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

    // Background painted by the CssRect base (defaultColor fallback).

    Text {
        anchors.centerIn: parent
        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#101418"
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: root.capsActive
        text: root.capsActive ? root.shortLayout.toUpperCase() : root.shortLayout
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
