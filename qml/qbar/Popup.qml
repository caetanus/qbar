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
    // When set, this popup is reachable over the JSON IPC by this name
    // (e.g. `{"command":"toggle","popup":"cpu"}` from a keyboard shortcut).
    property string name: ""
    // Title for the toplevel window created when this popup is DETACHED (so it isn't the
    // generic "QBar Detached"). e.g. "QBar Bitcoin Applet — BTC/USDT".
    property string windowTitle: ""
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
        // Carry the detached-window title in the payload so the service can title the toplevel
        // it creates on detach (the spec stores the payload). Copy so we don't mutate `payload`.
        var pl = payload
        if (windowTitle.length > 0) {
            pl = {}
            for (var k in payload)
                pl[k] = payload[k]
            pl.windowTitle = windowTitle
        }
        popupId = kind === "tooltip"
            ? qbarPopups.openTooltip(source, pl, point.x, point.y, popupWidth, popupHeight, popupId)
            : qbarPopups.openPopup(source, pl, point.x, point.y, popupWidth, popupHeight, popupId)
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

    function detach() {
        if (kind === "tooltip" || popupId.length === 0)
            return false
        return qbarPopups.detachPopup(popupId)
    }

    function toggle() {
        if (isOpen) {
            close()
        } else {
            open()
        }
    }

    Component.onCompleted: {
        if (name.length > 0 && typeof qbarIpc !== "undefined" && qbarIpc)
            qbarIpc.registerPopup(name, root)
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
