import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root

    property Item anchorItem
    property bool hovered: false
    property string text: ""
    property int delay: 300
    property int gap: 8
    property int textPaddingX: 10
    property int textPaddingY: 6
    property int popupMinWidth: 72
    property string side: "auto"
    property string tooltipId: ""

    readonly property int popupWidth: Math.max(popupMinWidth, Math.ceil(measureText.implicitWidth + textPaddingX * 2))
    readonly property int popupHeight: Math.max(28, Math.ceil(measureText.implicitHeight + textPaddingY * 2))
    readonly property bool isOpen: tooltipId.length > 0

    function refresh() {
        if (tooltipId.length === 0) {
            return
        }

        qbarPopups.updateTooltip(tooltipId, ({ text: root.text }))
    }

    function open() {
        console.warn("[tooltip] open", text, popupWidth, popupHeight)
        if (tooltipId.length > 0) {
            refresh()
            return tooltipId
        }

        tooltipId = tooltipPopup.open()
        refresh()
        return tooltipId
    }

    function close() {
        if (tooltipId.length === 0) {
            return
        }
        console.warn("[tooltip] close", text)
        tooltipPopup.close()
        tooltipId = ""
    }

    function syncVisibility() {
        console.warn("[tooltip] sync", text, "hovered:", hovered, "open:", tooltipPopup.isOpen)
        if (hovered) {
            hoverTimer.restart()
        } else {
            hoverTimer.stop()
            root.close()
        }
    }

    onHoveredChanged: syncVisibility()
    onTextChanged: {
        refresh()
    }

    QBar.Popup {
        id: tooltipPopup
        kind: "tooltip"
        anchorItem: root.anchorItem ? root.anchorItem : root
        source: "qrc:/popups/TooltipPopup.qml"
        payload: ({ text: root.text })
        popupWidth: root.popupWidth
        popupHeight: root.popupHeight
        gap: root.gap
        placement: "below"
        horizontalAlignment: "left"

        onPopupOpened: {
            console.warn("[tooltip] popup opened", text, id)
            root.tooltipId = id
            root.refresh()
            root.opened(id)
        }
        onClosed: {
            console.warn("[tooltip] popup closed", text)
            root.tooltipId = ""
            root.closed()
        }
    }

    Timer {
        id: hoverTimer
        interval: root.delay
        repeat: false
        onTriggered: {
            console.warn("[tooltip] open timer", root.text, root.hovered)
            if (root.hovered) {
                root.open()
            }
        }
    }

    Text {
        id: measureText
        visible: false
        text: root.text
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: true
    }

    signal opened(string id)
    signal closed()
}
