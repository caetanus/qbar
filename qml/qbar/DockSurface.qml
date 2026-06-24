import QtQuick

// Contents of the macOS-style Dock window (created by DockWindow, hosted in its OWN
// surface so the magnification can overflow the bar). Renders the running windows as a
// row of app icons that magnify toward the cursor; click focuses the window.
//
// The effect is pluggable via `effect`: "magnify" (cursor fisheye) is implemented now;
// "none" disables it, and bounce/genie/etc. hook into effectSize()/the delegate later.
//
// The window is sized to (slot width × bar height + headroom) and anchored to the bar
// edge; icons sit on that edge and grow into the headroom. Constants here mirror the
// in-bar proxy (applets/Dock.qml) so the reserved width matches what we draw.
Item {
    id: root

    readonly property int barHeight: theme.height
    property string effect: "magnify"
    property real iconBase: Math.round(root.barHeight * 0.78)
    property real iconMax: root.iconBase * 2.0      // peak magnified size
    property real influence: root.iconBase * 2.6    // cursor falloff radius (px)
    property real spacing: 6
    property real cursorX: -1e6                      // cursor X in row coords; <0 → no magnify

    // Whether the bar (and so this dock) sits at the bottom — icons grow upward.
    readonly property bool bottomEdge: true

    function effectSize(centerX) {
        if (root.effect !== "magnify" || root.cursorX < -9.9e5)
            return root.iconBase
        var d = Math.abs(centerX - root.cursorX)
        if (d >= root.influence)
            return root.iconBase
        var t = 0.5 * (1.0 + Math.cos(Math.PI * d / root.influence))
        return root.iconBase + (root.iconMax - root.iconBase) * t
    }

    // Optional dock panel behind the icons (themeable later); subtle by default.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(0, (root.barHeight - root.iconBase) / 2 - 2)
        anchors.horizontalCenter: parent.horizontalCenter
        width: row.width + 12
        height: root.iconBase + 8
        radius: 10
        color: Qt.rgba(0, 0, 0, 0.18)
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        visible: row.children.length > 0
    }

    Row {
        id: row
        anchors.bottom: parent.bottom
        // Centre the icon baseline on the bar's strip; magnified icons rise above it.
        anchors.bottomMargin: Math.max(0, (root.barHeight - root.iconBase) / 2)
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

                width: root.iconBase
                height: root.iconBase
                readonly property real centerX: row.x + x + width / 2
                readonly property real sz: root.effectSize(centerX)

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: cell.sz
                    height: cell.sz
                    sourceSize.width: Math.ceil(root.iconMax)
                    sourceSize.height: Math.ceil(root.iconMax)
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

                // Running indicator dot under the icon.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom
                    anchors.topMargin: 2
                    width: 3; height: 3; radius: 1.5
                    color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.5)
                }
            }
        }
    }

    // Single tracker for magnification + click routing (mapped to the row's coords).
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onPositionChanged: function (m) { root.cursorX = m.x - row.x }
        onExited: root.cursorX = -1e6
        onClicked: function (m) {
            if (!wm)
                return
            var rx = m.x - row.x
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
