import QtQuick
import "qrc:/qbar" as QBar

// Root of the toast strip (one full-height layer surface on a screen corner).
// NotificationWindow reads `stackHeight` to shrink the wl input region to the card
// stack, so the transparent rest of the strip passes clicks through.
//
// CSS hooks:
//   #notifications              — width / margin-* (read by NotificationWindow), spacing
//   #notification               — the card (NotificationCard.qml), incl. entry `animation`
//   #notification:exit          — exit `animation: <keyframes> <ms> <easing>` (opacity/
//                                 transform, like the entry), or `transition` for just
//                                 the fade/slide fallback's duration + easing
Item {
    id: root

    readonly property real stackHeight: Math.min(list.contentHeight, root.height)

    readonly property var stackStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("notifications") : ({})
    readonly property real cardSpacing: stackStyle["spacing"]
        ? cssTheme.parseLength(stackStyle["spacing"], 10) : 10

    readonly property string corner: (notifConfig && notifConfig.corner) ? notifConfig.corner : "top-right"
    readonly property bool bottomCorner: corner.startsWith("bottom")
    readonly property bool leftCorner: corner.endsWith("left")
    readonly property int maxVisible: (notifConfig && notifConfig.maxVisible !== undefined)
        ? notifConfig.maxVisible : 5

    // Exit timing: `#notification:exit { animation: <keyframes> <ms> <easing> }` wins;
    // `transition: opacity <ms> <easing>` still sets the fallback fade/slide's params.
    // (The keyframes themselves are interpolated per-card — see NotificationCard.)
    readonly property var exitStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("notification", ["exit"]) : ({})
    readonly property var exitAnim: exitStyle["animation"]
        ? cssTheme.parseAnimation(exitStyle["animation"]) : ({})
    readonly property var exitTransition: exitStyle["transition"]
        ? cssTheme.parseTransition(exitStyle["transition"]) : ({})
    readonly property int exitMs: exitAnim.duration !== undefined ? exitAnim.duration
        : (exitTransition.duration !== undefined ? exitTransition.duration : 220)
    readonly property int exitEasing: exitAnim.easing !== undefined ? exitAnim.easing
        : (exitTransition.easing !== undefined ? exitTransition.easing : Easing.InQuad)

    ListView {
        id: list
        anchors.fill: parent
        interactive: false
        clip: false
        spacing: root.cardSpacing
        verticalLayoutDirection: root.bottomCorner ? ListView.BottomToTop : ListView.TopToBottom
        model: notificationModel

        delegate: QBar.NotificationCard {
            width: list.width
            visibleInStack: index < root.maxVisible
            slideFromLeft: root.leftCorner
        }

        // Cards animate their own ENTRY (CSS `#notification { animation: ... }`,
        // see NotificationCard); the view animates removal and the resulting shifts.
        displaced: Transition {
            NumberAnimation {
                properties: "y"
                duration: root.exitMs
                easing.type: Easing.OutQuad
            }
        }
        remove: Transition {
            // Drives the card's exitProgress; the card maps it through the exit
            // @keyframes (or the fade + slide-off-edge fallback).
            NumberAnimation {
                property: "exitProgress"
                from: 0.0
                to: 1.0
                duration: root.exitMs
                easing.type: root.exitEasing
            }
        }
    }
}
