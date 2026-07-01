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

    // Animation modes (config-selectable via the "dock" config block, forwarded by
    // DockWindow as the `dockConfig` context property):
    //   magnify   — hover effect: "fisheye" (cosine peak under the cursor, default),
    //               "parabolic" (sharper, more pointed peak), "scale" (whole dock grows
    //               uniformly, no per-icon peak), "coverflow" (macOS Cover Flow: the icon
    //               under the cursor faces front, neighbours rotate around the vertical
    //               axis into 3D perspective depth, over a gentle grow), "none".
    //   indicator — focused-window marker: "underline" (accent bar, default), "dot"
    //               (round dots), "pill" (translucent accent tile behind the icon),
    //               "none".
    readonly property string magnifyMode: (typeof dockConfig !== "undefined" && dockConfig && dockConfig.magnify)
        ? dockConfig.magnify : "fisheye"
    readonly property string indicatorMode: (typeof dockConfig !== "undefined" && dockConfig && dockConfig.indicator)
        ? dockConfig.indicator : "underline"
    // Whole dock grows on hover for everything except "none".
    readonly property bool magnifies: root.magnifyMode !== "none"
    // Per-icon peak under the cursor (fisheye/parabolic/coverflow); "scale" grows uniformly.
    readonly property bool fisheye: root.magnifyMode === "fisheye" || root.magnifyMode === "parabolic"
                                    || root.magnifyMode === "coverflow"
    readonly property bool coverflow: root.magnifyMode === "coverflow"
    // Cover Flow tuning: neighbours rotate up to ±maxAngle around the vertical axis, with a
    // perspective divide at `depth` px so the far edge genuinely recedes (real 3D, not affine).
    property real coverflowMaxAngle: 58
    property real coverflowDepth: 650

    property real hoverHeight: (typeof dockConfig !== "undefined" && dockConfig && dockConfig.hoverHeight > 0)
        ? dockConfig.hoverHeight : 48                    // whole-dock baseline height on hover
    property real peakHeight: (typeof dockConfig !== "undefined" && dockConfig && dockConfig.peakHeight > 0)
        ? dockConfig.peakHeight : Math.round(root.hoverHeight * 1.5)  // cursor-focused icon (fisheye peak)
    property real spacing: 6
    property bool hovered: false
    property real cursorX: -1e6                          // cursor X in row coords (<0 → unknown)
    property real influence: root.hoverHeight * 2.6      // fisheye falloff radius (px)
    property real slotCenterX: width / 2                 // set by DockWindow; surface stays stable
    property real slotWidth: 0                           // visual width reserved by Dock.qml
    property real edgePadding: Math.max(root.peakHeight, root.influence * 0.5)

    // Uniform baseline: bar height at rest → hoverHeight on hover, animated.
    property real baseSize: (root.hovered && root.magnifies) ? Math.max(root.barHeight, root.hoverHeight) : root.barHeight
    Behavior on baseSize { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

    // Per-icon fisheye ON TOP of the baseline: the icon at the cursor reaches peakHeight,
    // decaying to baseSize past `influence`. At rest (not hovered) everything is bar height.
    function iconSize(centerX) {
        if (!root.hovered || !root.magnifies)
            return root.barHeight
        if (!root.fisheye)                               // "scale": uniform grow, no peak
            return root.baseSize
        if (root.cursorX < -9.9e5)
            return root.baseSize
        var d = Math.abs(centerX - root.cursorX)
        if (d >= root.influence)
            return root.baseSize
        var u = d / root.influence
        // Parabolic falls off as 1-u² (pointed peak); fisheye uses a raised cosine (softer).
        var t = root.magnifyMode === "parabolic"
            ? (1.0 - u * u)
            : 0.5 * (1.0 + Math.cos(Math.PI * u))
        // Cover Flow leans on 3D depth, so its grow is gentler than the plain fisheye peak.
        var peak = root.coverflow ? (root.baseSize + (root.peakHeight - root.baseSize) * 0.5) : root.peakHeight
        return root.baseSize + (peak - root.baseSize) * t
    }

    // Cover Flow transform for a cell: rotate `angleDeg` around the vertical (Y) axis about
    // the cell's horizontal centre, with a perspective divide so the receding edge shrinks.
    // Composed as translate-to-centre · perspective · rotateY · translate-back. angle 0 →
    // identity, so attaching this unconditionally is a no-op for the other magnify modes.
    function coverflowMatrix(angleDeg, w, h) {
        if (!angleDeg)
            return Qt.matrix4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1)
        var a = angleDeg * Math.PI / 180
        var cx = w / 2, cy = h / 2, c = Math.cos(a), s = Math.sin(a)
        var t1 = Qt.matrix4x4(1,0,0,-cx, 0,1,0,-cy, 0,0,1,0, 0,0,0,1)
        var r  = Qt.matrix4x4(c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1)
        var p  = Qt.matrix4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-1 / root.coverflowDepth,1)
        var t2 = Qt.matrix4x4(1,0,0,cx, 0,1,0,cy, 0,0,1,0, 0,0,0,1)
        return t2.times(p).times(r).times(t1)
    }

    ListView {
        id: row
        anchors.bottom: parent.bottom               // the bar edge
        x: Math.round(root.slotCenterX - width / 2)
        width: Math.max(1, contentWidth, root.slotWidth - 16)
        height: root.peakHeight + 6
        orientation: ListView.Horizontal
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: false
        spacing: root.spacing
        model: windowModel ? windowModel : 0

        // Smooth a window opening/closing: the new icon grows in from the bar edge and
        // the neighbours slide over, instead of popping in and snapping the row.
        add: Transition {
            NumberAnimation { property: "scale"; from: 0.0; to: 1.0; duration: 160; easing.type: Easing.OutBack }
        }
        remove: Transition {
            NumberAnimation { property: "scale"; to: 0.0; duration: 140; easing.type: Easing.InCubic }
        }
        displaced: Transition {
            NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutCubic }
        }

        delegate: Item {
            id: cell
            required property var windowId
            required property string appId
            required property string title
            required property bool focused
            required property bool urgent

            width: root.baseSize                 // uniform slot — icon scales within/over it
            height: row.height
            transformOrigin: Item.Bottom         // grow-in/out animation uses the bar edge
            readonly property real centerX: x + width / 2   // content-local, stable (uniform slots)
            readonly property real sz: root.iconSize(centerX)
            opacity: cell.focused || cell.urgent ? 1.0 : 0.72

            // Cover Flow: rotate this card around the vertical axis by its signed distance
            // from the cursor (flat at the cursor, ±maxAngle past `influence`). 0 for every
            // other magnify mode (and at rest), so the Matrix4x4 below is then identity.
            readonly property real coverflowAngle: (root.coverflow && root.hovered && root.cursorX > -9.9e5)
                ? root.coverflowMaxAngle * Math.max(-1, Math.min(1, (cell.centerX - root.cursorX) / root.influence))
                : 0
            Behavior on coverflowAngle { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            transform: Matrix4x4 { matrix: root.coverflowMatrix(cell.coverflowAngle, cell.width, cell.height) }

            // Urgent windows bounce for attention (macOS dock idiom): the icon hops
            // up out of the bar and settles, repeating while the window stays urgent.
            property real bounceY: 0
            SequentialAnimation {
                running: cell.urgent
                loops: Animation.Infinite
                onRunningChanged: if (!running) cell.bounceY = 0
                NumberAnimation { target: cell; property: "bounceY"; to: -root.barHeight * 0.6; duration: 260; easing.type: Easing.OutQuad }
                NumberAnimation { target: cell; property: "bounceY"; to: 0; duration: 420; easing.type: Easing.OutBounce }
                PauseAnimation { duration: 900 }
            }

            // "pill" indicator: a translucent accent tile behind the focused icon,
            // sized to the (magnified) icon so it tracks the fisheye. Declared first so
            // it paints behind the icon.
            Rectangle {
                visible: root.indicatorMode === "pill" && cell.focused
                width: cell.sz + 8
                height: cell.sz + 8
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -4
                radius: Math.round(width * 0.28)
                color: theme.accent
                opacity: 0.22
                transform: Translate { y: cell.bounceY }
            }

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
                transform: Translate { y: cell.bounceY }
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
                transform: Translate { y: cell.bounceY }
            }

            // Focused/running indicator below the icon. "underline" widens an accent
            // bar for the focused window (faint thin bar otherwise); "dot" marks each
            // running window with a round dot, accent + larger for the focused one.
            // "pill" (handled above) and "none" leave the bottom marker hidden.
            Rectangle {
                visible: root.indicatorMode === "underline" || root.indicatorMode === "dot"
                readonly property bool asDot: root.indicatorMode === "dot"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: asDot ? (cell.focused ? 7 : 5)
                             : (cell.focused ? Math.round(cell.width * 0.5) : 3)
                height: asDot ? width : 3
                radius: asDot ? width / 2 : 1.5
                color: cell.focused ? theme.accent : Qt.rgba(1, 1, 1, 0.45)
                Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
            }
        }
    }

    // Hover band: drives the grow + fisheye and routes clicks. Anchored to the bar edge.
    // At REST it hugs the resting dock (bar height, icon width) so the empty space ABOVE
    // and beside the dock doesn't trigger the fisheye. Once hovered it expands to cover the
    // magnified peak (and the edge padding) so the cursor stays "in" while icons grow upward.
    MouseArea {
        id: hover
        anchors.bottom: parent.bottom
        width: root.hovered
            ? Math.max(row.width + 2 * root.edgePadding, root.barHeight, root.slotWidth + 2 * root.edgePadding)
            : Math.max(row.width, root.barHeight, root.slotWidth)
        x: Math.round(root.slotCenterX - width / 2)
        height: root.hovered ? root.peakHeight + 6 : root.barHeight
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onContainsMouseChanged: root.hovered = containsMouse
        onPositionChanged: function (m) { root.cursorX = hover.mapToItem(row.contentItem, m.x, m.y).x }
        onExited: root.cursorX = -1e6
        onClicked: function (m) {
            if (!wm)
                return
            var rx = hover.mapToItem(row.contentItem, m.x, m.y).x
            for (var i = 0; i < row.contentItem.children.length; i++) {
                var c = row.contentItem.children[i]
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
