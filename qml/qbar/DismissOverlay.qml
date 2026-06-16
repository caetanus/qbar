import QtQuick

Item {
    id: root

    signal dismissed()

    // Window-scoped so it works regardless of which popup item holds activeFocus
    // (e.g. the calendar grabs focus for date navigation). Only fires when the
    // overlay grabbed the keyboard (BarConfig::popupKeyboardFocus).
    Shortcut {
        sequences: ["Escape"]
        onActivated: root.dismissed()
    }

    // Popups are reparented here by QBarPopupService (positioned in overlay-local
    // coordinates). Declared above the dismiss MouseArea so popup clicks win.
    property alias popupLayer: popupLayer

    // CSS-configurable backdrop (#overlay). Default fully transparent: the
    // backdrop only catches outside clicks unless the theme opts into a dim.
    readonly property var overlayStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("overlay") : ({})

    Rectangle {
        anchors.fill: parent
        color: root.overlayStyle["background-color"]
            ? cssTheme.parseColor(root.overlayStyle["background-color"])
            : "transparent"
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons

        onPressed: function(mouse) {
            mouse.accepted = true
            root.dismissed()
        }

        onWheel: function(wheel) {
            wheel.accepted = true
            root.dismissed()
        }
    }

    Item {
        id: popupLayer
        anchors.fill: parent
    }
}
