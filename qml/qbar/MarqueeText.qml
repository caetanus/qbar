import QtQuick

// When the text fits, it renders statically. When it overflows, it scrolls as a
// continuous ticker: a "|"-separated second copy follows so the wrap is seamless
// (no jump back to the start), pausing briefly at each cycle boundary. Set
// `width` to the available space; read `contentWidth` for the full text width.
Item {
    id: root

    property string text: ""
    property color color: "white"
    property font font
    property int pixelsPerSecond: 40
    property int pauseDuration: 1000
    property string separator: "  |  "

    readonly property real contentWidth: label.implicitWidth
    readonly property bool overflowing: label.implicitWidth > width + 0.5

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight
    clip: true

    Row {
        id: track
        x: 0

        Text {
            id: label
            height: root.height
            verticalAlignment: Text.AlignVCenter
            text: root.text
            color: root.color
            font: root.font
        }
        Text {
            height: root.height
            verticalAlignment: Text.AlignVCenter
            visible: root.overflowing
            text: root.separator
            color: root.color
            font: root.font
        }
        Text {
            // Trailing copy makes the loop wrap seamlessly while scrolling.
            id: separatorLabel
            height: root.height
            verticalAlignment: Text.AlignVCenter
            visible: root.overflowing
            text: root.text
            color: root.color
            font: root.font
        }
    }

    // Scroll until the end of the text reaches the right edge, then pause there;
    // afterwards keep scrolling so the separator + trailing copy slide in and the
    // wrap is seamless (the trailing copy lands where the first one started).
    readonly property real endVisibleX: root.width - label.implicitWidth

    // All three children are Animators so the SequentialAnimation is itself treated
    // as an Animator and runs on the scene-graph render thread — it keeps gliding even
    // when the GUI thread is briefly blocked (custom-tool refreshes, JSON parses), which
    // is what caused the marquee's "travadinhas". Note: a single non-Animator child (e.g.
    // a PauseAnimation) would demote the WHOLE group back to the GUI thread, so the pause
    // is expressed as a no-op hold Animator (from === to) instead of PauseAnimation.
    // (Render-thread execution requires the threaded render loop, which qbar uses.)
    SequentialAnimation {
        running: root.overflowing && root.visible
        loops: Animation.Infinite

        XAnimator {
            target: track
            from: 0
            to: root.endVisibleX
            duration: Math.max(1, (label.implicitWidth - root.width) / root.pixelsPerSecond * 1000)
            easing.type: Easing.Linear
        }
        XAnimator {
            target: track // hold at the end edge for the pause (no movement)
            from: root.endVisibleX
            to: root.endVisibleX
            duration: root.pauseDuration
        }
        XAnimator {
            target: track
            from: root.endVisibleX
            to: -separatorLabel.x
            duration: Math.max(1, (separatorLabel.x + root.endVisibleX) / root.pixelsPerSecond * 1000)
            easing.type: Easing.Linear
        }
    }

    onOverflowingChanged: if (!overflowing) track.x = 0
    onTextChanged: track.x = 0
}
