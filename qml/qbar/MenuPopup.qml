import QtQuick

Item {
    id: root

    property Item anchorItem
    property var model: []
    property string menuId: ""
    property int gap: 0
    property string placement: "below"
    property string horizontalAlignment: "left"
    readonly property bool isOpen: menuId.length > 0
    width: 0
    height: 0
    visible: false

    signal menuOpened(string id)
    signal triggered(int index, var item)

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
            localX = anchorItem.width
        } else if (horizontalAlignment === "center") {
            localX = Math.round(anchorItem.width / 2)
        }

        var localY = anchorItem.height + gap
        if (placement === "above") {
            localY = -gap
        }

        return globalPointForItem(anchorItem, localX, localY)
    }

    function open() {
        var point = anchorPoint()
        menuId = qbarPopups.openMenu(model, point.x, point.y, menuId)
        if (menuId.length > 0) {
            menuOpened(menuId)
        }
        return menuId
    }

    function close() {
        qbarPopups.closeAll()
        menuId = ""
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
        function onMenuTriggered(id, index, item) {
            if (id === root.menuId) {
                root.triggered(index, item)
                root.menuId = ""
            }
        }
    }
}
