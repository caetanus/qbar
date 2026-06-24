import QtQuick

// Contents of the macOS-style Dock window (created by DockWindow, hosted in its OWN
// surface so it can overflow the bar). Renders the running windows as a row of app
// icons; click focuses the window.
//
// Behaviour (per spec):
//   • At rest the dock is the BAR's height — icons fill the bar like a normal applet.
//   • On hover the WHOLE dock grows uniformly to `hoverHeight` (animated) — every icon
//     grows together, overflowing ON TOP of the bar (the separate window allows it).
//   • The focused window's icon is highlighted (full opacity + an accent underline);
//     the others are slightly dimmed.
Item {
    id: root

    readonly property int barHeight: theme.height
    property real hoverHeight: 48                 // whole-dock height on hover (try 32 or 48)
    property real spacing: 6
    property bool hovered: false

    // Whole-dock uniform grow: bar height at rest → hoverHeight on hover, animated.
    property real cellSize: root.hovered ? Math.max(root.barHeight, root.hoverHeight) : root.barHeight
    Behavior on cellSize { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

    Row {
        id: row
        anchors.bottom: parent.bottom               // the bar edge
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.spacing

        Repeater {
            model: windowModel ? windowModel : 0
            delegate: Item {
                id: cell
                required property var windowId
                required property string appId
                required property string title
                required property bool focused

                width: root.cellSize
                height: root.cellSize
                // Focus effect: focused window stands out, others dim back.
                opacity: cell.focused ? 1.0 : 0.72

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: parent.width
                    height: parent.width - 4            // leave room for the indicator
                    sourceSize.width: Math.ceil(root.hoverHeight)
                    sourceSize.height: Math.ceil(root.hoverHeight)
                    fillMode: Image.PreserveAspectFit
                    source: cell.appId.length > 0 ? "image://themeicon/" + cell.appId : ""
                    visible: status === Image.Ready
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    height: parent.width - 4
                    verticalAlignment: Text.AlignVCenter
                    visible: !icon.visible
                    text: cell.appId.length > 0 ? cell.appId.charAt(0).toUpperCase() : "?"
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pointSize: Math.max(8, Math.round(cell.height * 0.4))
                }

                // Focused/running indicator: accent underline for the focused window,
                // a faint dot for the other running windows.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: cell.focused ? Math.round(parent.width * 0.5) : 3
                    height: 3
                    radius: 1.5
                    color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.45)
                    Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                }
            }
        }
    }

    // Hover band: only the dock's own area (not the whole tall window) drives the grow
    // and routes clicks. Anchored to the bar edge, sized to the grown dock.
    MouseArea {
        id: hover
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(row.width + 2 * root.spacing, root.barHeight)
        height: root.hoverHeight + 6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onContainsMouseChanged: root.hovered = containsMouse
        onClicked: function (m) {
            if (!wm)
                return
            var rx = hover.mapToItem(row, m.x, m.y).x
            for (var i = 0; i < row.children.length; i++) {
                var c = row.children[i]
                if (c.windowId === undefined)
                    continue
                if (rx >= c.x && rx < c.x + c.width) {
                    wm.activateWindow(c.windowId)
                    return
                }
            }
        }
    }
}
