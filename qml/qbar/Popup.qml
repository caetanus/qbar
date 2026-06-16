import QtQuick

Item {
    id: root

    property Item anchorItem
    property url source
    property var payload: ({})
    property var explicitPosition: null
    // "popup" → backdrop overlay (modal, dismissable). "tooltip" → ordinary
    // standalone window (no backdrop, no keyboard), via openTooltip.
    property string kind: "popup"
    property string popupId: ""
    property int popupWidth: 240
    property int popupHeight: 160
    property int gap: 0
    property string placement: "below"
    property string horizontalAlignment: "left"
    readonly property bool isOpen: popupId.length > 0
    width: 0
    height: 0
    visible: false

    signal popupOpened(string id)
    signal closed()

    function globalPointForItem(item, localX, localY) {
        if (!item) {
            return Qt.point(0, 0)
        }

        if (typeof item.mapToGlobal === "function") {
            return item.mapToGlobal(Qt.point(localX, localY))
        }

        if (item.window && item.window.contentItem && typeof item.mapToItem === "function") {
            var scenePoint = item.mapToItem(item.window.contentItem, localX, localY)
            if (scenePoint !== undefined && scenePoint !== null) {
                return Qt.point(item.window.x + scenePoint.x, item.window.y + scenePoint.y)
            }
        }

        return Qt.point(0, 0)
    }

    function anchorPoint() {
        if (!anchorItem) {
            return Qt.point(0, 0)
        }

        var localX = 0
        if (horizontalAlignment === "right") {
            localX = anchorItem.width - popupWidth
        } else if (horizontalAlignment === "center") {
            localX = Math.round((anchorItem.width - popupWidth) / 2)
        }

        var localY = anchorItem.height + gap
        if (placement === "above") {
            localY = -popupHeight - gap
        }

        return globalPointForItem(anchorItem, localX, localY)
    }

    function open() {
        var point = explicitPosition && explicitPosition.x !== undefined && explicitPosition.y !== undefined
            ? explicitPosition
            : anchorPoint()
        popupId = kind === "tooltip"
            ? qbarPopups.openTooltip(source, payload, point.x, point.y, popupWidth, popupHeight, popupId)
            : qbarPopups.openPopup(source, payload, point.x, point.y, popupWidth, popupHeight, popupId)
        if (popupId.length > 0) {
            popupOpened(popupId)
        }
        return popupId
    }

    function close() {
        if (popupId.length === 0) {
            return
        }

        if (kind === "tooltip") {
            qbarPopups.closeTooltip(popupId)
        } else {
            qbarPopups.closePopup(popupId)
        }
        popupId = ""
        closed()
    }

    function toggle() {
        if (isOpen) {
            close()
        } else {
            open()
        }
    }

    Connections {
        target: qbarPopups
        function onPopupClosed(id) {
            if (root.kind !== "tooltip" && id === root.popupId) {
                root.popupId = ""
                root.closed()
            }
        }
        function onTooltipClosed(id) {
            if (root.kind === "tooltip" && id === root.popupId) {
                root.popupId = ""
                root.closed()
            }
        }
    }
}
