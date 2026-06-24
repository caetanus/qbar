import QtQuick
import "qrc:/qbar" as QBar

// macOS-style dock (first cut, as a bar applet). Renders the running windows as a
// row of app icons that magnify toward the cursor. Click focuses the window.
//
// NOTE: as a parented applet the upward magnification is clipped at the bar's top
// edge — the planned next step is to host this in its OWN window (override-redirect
// / layer-shell) positioned over a reserved bar slot, so icons can grow past the bar
// like the real Dock. The effect math here is written to carry over unchanged.
//
// The effect is pluggable via `effect`: "magnify" (cursor zoom) is implemented;
// "none" disables it; bounce/genie/etc. hook into effectSize()/the delegate later.
QBar.CssRect {
    id: root
    cssId: "dock"
    height: theme.height
    clip: false

    readonly property var cssStyle: root.style

    property string effect: cssStyle["qbar-dock-effect"] || "magnify"
    property real iconBase: Math.round(root.height * 0.78)
    property real iconMax: root.iconBase * 2.0      // peak magnified size
    property real influence: root.iconBase * 2.6    // cursor falloff radius (px)
    property real spacing: 6
    property real cursorX: -1e6                     // cursor X in dock coords; <0 → no magnify

    property int preferredWidth: row.implicitWidth > 0 ? Math.ceil(row.implicitWidth + 16) : 0
    width: Math.max(1, preferredWidth)
    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // Size of an icon given its centre X (dock coords): a cosine fisheye that peaks
    // at the cursor and decays to iconBase past `influence`.
    function effectSize(centerX) {
        if (root.effect !== "magnify" || root.cursorX < -9.9e5)
            return root.iconBase
        var d = Math.abs(centerX - root.cursorX)
        if (d >= root.influence)
            return root.iconBase
        var t = 0.5 * (1.0 + Math.cos(Math.PI * d / root.influence))
        return root.iconBase + (root.iconMax - root.iconBase) * t
    }

    Row {
        id: row
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 8
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
                height: root.height
                // Centre of this slot in dock coordinates (fixed-slot layout: the
                // icon scales visually without reflowing neighbours).
                readonly property real centerX: row.x + x + width / 2
                readonly property real sz: root.effectSize(centerX)

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
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

                // Fallback glyph when the app has no themed icon.
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    visible: !icon.visible
                    text: cell.appId.length > 0 ? cell.appId.charAt(0).toUpperCase() : "?"
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pointSize: Math.max(8, Math.round(cell.sz * 0.4))
                }

                // Running indicator dot.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: 3; height: 3; radius: 1.5
                    color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.5)
                }
            }
        }
    }

    // Single tracker for magnification + click routing (avoids per-icon MouseAreas
    // stealing hover from each other).
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPositionChanged: function (m) { root.cursorX = m.x }
        onExited: root.cursorX = -1e6
        onClicked: function (m) {
            if (!wm)
                return
            for (var i = 0; i < row.children.length; i++) {
                var c = row.children[i]
                if (c.windowId === undefined)
                    continue
                var left = row.x + c.x
                if (m.x >= left && m.x < left + c.width) {
                    wm.activateWindow(c.windowId)
                    return
                }
            }
        }
    }
}
