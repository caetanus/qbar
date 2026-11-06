import QtQuick
import "qrc:/qbar" as QBar

// Root of the toast strip (one full-height layer surface on a screen corner).
// NotificationWindow reads `stackHeight` to shrink the wl input region to the card
// stack, so the transparent rest of the strip passes clicks through.
//
// CSS hooks:
//   #notifications              — width / margin-* (read by NotificationWindow), spacing
//   #notifications.clear-all    — the "clear all" pill shown ahead of the stack when
//                                 more than one card is up (background, color, border…,
//                                 :hover state)
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

    // Exit timing lives in NotificationCard (each card runs its own exit); the view
    // only animates the shift of the cards left behind.
    readonly property var exitStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("notification", ["exit"]) : ({})
    readonly property var exitTransition: exitStyle["transition"]
        ? cssTheme.parseTransition(exitStyle["transition"]) : ({})
    readonly property int exitMs: exitTransition.duration !== undefined ? exitTransition.duration : 220

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

        // "Clear all" pill, shown ahead of the stack (the header sits at the
        // list's start — nearest the corner for bottom stacks too) once there
        // is more than one card. Collapsed via height, not `visible`, so the
        // stackHeight input region follows it.
        header: Item {
            readonly property bool active: notificationModel.count > 1
            width: list.width
            height: active ? clearPill.height + root.cardSpacing : 0
            clip: true

            // Themes style the pill via #notifications.clear-all; without such a
            // rule it wears the CARD's chrome (#notification + the same solid
            // default) so it stays readable over any wallpaper — a floating pill
            // with a translucent default was invisible on themes that never
            // mention it.
            readonly property var clearStyle: cssTheme && cssTheme.loaded
                ? cssTheme.resolvePart("notifications", "clear-all") : ({})
            readonly property bool clearThemed: clearStyle["background-color"] !== undefined
                || clearStyle["background"] !== undefined

            Item {
                id: clearPill
                // Full card width: the pill reads as one more card in the stack,
                // not a floating chip beside it.
                width: parent.width
                height: clearLabel.implicitHeight + 12

                QBar.CssRect {
                    anchors.fill: parent
                    cssId: parent.parent.clearThemed ? "notifications" : "notification"
                    cssPart: parent.parent.clearThemed ? "clear-all" : ""
                    cssClass: clearMouse.containsMouse ? ["hover"] : []
                    radius: height / 2
                    defaultColor: "#2b3140"
                    defaultBorderColor: Qt.rgba(1, 1, 1, 0.14)
                    defaultBorderWidth: 1
                }
                QBar.CssText {
                    id: clearLabel
                    anchors.centerIn: parent
                    // Unthemed fallback mirrors the CssRect above: wear the CARD's
                    // body text style (font-size/color) so the pill reads like the
                    // notifications it clears — the bare "notifications"/"clear-all"
                    // resolution is empty then, and the label fell back to the BAR's
                    // font, visibly bigger than any card text.
                    cssId: parent.parent.clearThemed ? "notifications" : "notification"
                    cssPart: parent.parent.clearThemed ? "clear-all" : "body"
                    cssClass: clearMouse.containsMouse ? ["hover"] : []
                    text: "✕  " + qsTr("Clear all") + " (" + notificationModel.count + ")"
                    defaultColor: theme.foreground
                }
                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: parent.parent.active
                    onClicked: notificationServer.dismissAll()
                }
            }
        }

        // Cards animate their own ENTRY and EXIT (CSS `#notification { animation }` /
        // `#notification:exit`, see NotificationCard); a card's row leaves the model only
        // after its exit finished, and its height collapses along the way, so the cards
        // behind it slide up smoothly. No `remove` transition on purpose: the view clears
        // everything at once when the model empties, transitions included.
    }
}
