import QtQuick

// Contents of the macOS-style Dock window (created by DockWindow, hosted in its OWN
// surface so it can overflow the bar). Renders the running windows as a row of app
// icons; click focuses the window.
//
// Behaviour (per spec):
//   • At rest the dock is the BAR's height — icons fill the bar like a normal applet.
//   • On hover the per-icon magnification animates (a cursor fisheye), and the whole
//     dock's height becomes `hoverHeight` (32px) — i.e. the icon under the cursor grows
//     to hoverHeight (the dock's tallest point), neighbours falling off to bar height.
// The separate window is what lets the grown icons overflow ON TOP of the bar.
//
// Fixed-width slots (no reflow): each icon scales in place toward the cursor, so the
// layout never jitters; magnified icons overflow their slot upward into the headroom.
Item {
    id: root

    readonly property int barHeight: theme.height
    property real hoverHeight: 32                       // peak icon size = dock height on hover
    property real spacing: 6
    property real influence: root.hoverHeight * 2.6     // cursor falloff radius (px)
    property real cursorX: -1e6                          // cursor X in row coords; <0 → rest (no magnify)

    // Cosine fisheye: bar height at rest / far from the cursor, growing to hoverHeight
    // right at the cursor. The dock's height == the tallest (cursor) icon.
    function iconSize(centerX) {
        if (root.cursorX < -9.9e5)
            return root.barHeight
        var d = Math.abs(centerX - root.cursorX)
        if (d >= root.influence)
            return root.barHeight
        var t = 0.5 * (1.0 + Math.cos(Math.PI * d / root.influence))
        return root.barHeight + (root.hoverHeight - root.barHeight) * t
    }

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

                width: root.barHeight                // fixed slot — icon scales within
                height: root.barHeight
                readonly property real centerX: x + width / 2   // row-local, fixed
                readonly property real sz: root.iconSize(centerX)

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: cell.sz
                    height: cell.sz
                    sourceSize.width: Math.ceil(root.hoverHeight)
                    sourceSize.height: Math.ceil(root.hoverHeight)
                    fillMode: Image.PreserveAspectFit
                    source: cell.appId.length > 0 ? "image://themeicon/" + cell.appId : ""
                    visible: status === Image.Ready
                    Behavior on width  { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                    Behavior on height { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    visible: !icon.visible
                    text: cell.appId.length > 0 ? cell.appId.charAt(0).toUpperCase() : "?"
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pointSize: Math.max(8, Math.round(cell.sz * 0.4))
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: 3; height: 3; radius: 1.5
                    color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.5)
                }
            }
        }
    }

    // Hover band: only the dock's own area (not the whole tall window) drives the
    // magnification and routes clicks. Anchored to the bar edge, sized to the dock.
    MouseArea {
        id: hover
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(row.width + 2 * root.spacing, root.barHeight)
        height: root.hoverHeight + 6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onPositionChanged: function (m) { root.cursorX = hover.mapToItem(row, m.x, m.y).x }
        onExited: root.cursorX = -1e6
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
