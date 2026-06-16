import QtQuick

Item {
    id: root
    height: theme.height

    readonly property string cssId: "mode"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    // i3/sway "binding mode" (e.g. "resize"). Other WM backends always report
    // "default", so this applet stays hidden outside of i3/sway binding modes.
    readonly property string modeName: i3Ipc && i3Ipc.bindingMode ? i3Ipc.bindingMode : "default"
    readonly property bool active: modeName !== "default" && modeName !== ""

    property int preferredWidth: active ? Math.ceil(label.implicitWidth) + 8 : 0
    width: preferredWidth
    clip: true

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    Rectangle {
        anchors.fill: parent
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#e74c3c"
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#ffffff"
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        text: root.modeName
    }
}
