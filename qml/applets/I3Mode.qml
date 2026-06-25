import QtQuick
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "mode"
    defaultColor: "#e74c3c"
    height: theme.height

    readonly property var cssStyle: root.style

    // i3/sway binding mode or Hyprland submap (e.g. "resize"). Other backends
    // report "default", so this applet stays hidden outside of modal states.
    readonly property string modeName: i3Ipc && i3Ipc.bindingMode ? i3Ipc.bindingMode : "default"
    readonly property bool active: modeName !== "default" && modeName !== ""

    property int preferredWidth: active ? Math.ceil(label.implicitWidth) + 8 : 0
    width: preferredWidth
    clip: true

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // Background painted by the CssRect base (defaultColor fallback).

    Text {
        id: label
        anchors.centerIn: parent
        color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : "#ffffff"
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        text: root.modeName
    }
}
