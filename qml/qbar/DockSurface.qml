import QtQuick

// Contents of the macOS-style Dock window (created by DockWindow, hosted in its OWN
// surface so it can overflow the bar). Renders the running windows as a row of app
// icons; click focuses the window.
//
// Behaviour (per spec):
//   • At rest the dock is the BAR's height — icons fill the bar like a normal applet.
//   • On hover the WHOLE dock grows uniformly to `hoverHeight` (animated baseline) AND,
//     on top of that, the icon under the cursor magnifies further to `peakHeight`
//     (a macOS fisheye), neighbours falling off to the hover baseline.
//   • The focused window's icon is highlighted (full opacity + accent underline); the
//     others dim back with a faint dot.
// The separate window is what lets the grown/magnified icons overflow on top of the bar.
//
// Fixed-width slots (= the uniform baseline) keep the layout stable; the icon image
// scales within/over its slot for the fisheye, so there's no reflow jitter.
Item {
    id: root

    readonly property int barHeight: theme.height
    property real hoverHeight: 48                       // whole-dock baseline height on hover
    property real peakHeight: Math.round(root.hoverHeight * 1.5)  // cursor-focused icon (fisheye peak)
    property real spacing: 6
    property bool hovered: false
    property real cursorX: -1e6                          // cursor X in row coords (<0 → unknown)
    property real influence: root.hoverHeight * 2.6      // fisheye falloff radius (px)

    // Uniform baseline: bar height at rest → hoverHeight on hover, animated.
    property real baseSize: root.hovered ? Math.max(root.barHeight, root.hoverHeight) : root.barHeight
    Behavior on baseSize { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

    // Per-icon fisheye ON TOP of the baseline: the icon at the cursor reaches peakHeight,
    // decaying to baseSize past `influence`. At rest (not hovered) everything is bar height.
    function iconSize(centerX) {
        if (!root.hovered)
            return root.barHeight
        if (root.cursorX < -9.9e5)
            return root.baseSize
        var d = Math.abs(centerX - root.cursorX)
        if (d >= root.influence)
            return root.baseSize
        var t = 0.5 * (1.0 + Math.cos(Math.PI * d / root.influence))
        return root.baseSize + (root.peakHeight - root.baseSize) * t
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

                width: root.baseSize                 // uniform slot — icon scales within/over it
                height: root.baseSize
                readonly property real centerX: x + width / 2   // row-local, stable (uniform slots)
                readonly property real sz: root.iconSize(centerX)
                opacity: cell.focused ? 1.0 : 0.72

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: cell.sz
                    height: cell.sz
                    sourceSize.width: Math.ceil(root.peakHeight)
                    sourceSize.height: Math.ceil(root.peakHeight)
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

                // Focused/running indicator: accent underline for the focused window,
                // a faint dot for the other running windows.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: cell.focused ? Math.round(cell.width * 0.5) : 3
                    height: 3
                    radius: 1.5
                    color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.45)
                    Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                }
            }
        }
    }

    // Hover band: drives the grow + fisheye and routes clicks. Anchored to the bar edge,
    // tall enough to cover the magnified peak so the cursor stays "in" while magnifying.
    MouseArea {
        id: hover
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(row.width + 2 * root.spacing, root.barHeight)
        height: root.peakHeight + 6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onContainsMouseChanged: root.hovered = containsMouse
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
