import QtQuick

Item {
    id: root

    property int targetWidth: 1
    property int targetHeight: 1
    property bool popupClosing: false
    property bool animateBounds: true

    width: 1
    height: 1
    clip: true

    onTargetWidthChanged: {
        width = Math.max(1, targetWidth)
    }

    onTargetHeightChanged: {
        height = Math.max(1, targetHeight)
    }

    onPopupClosingChanged: {
        if (popupClosing) {
            width = 1
            height = 1
        }
    }

    Component.onCompleted: {
        width = Math.max(1, targetWidth)
        height = Math.max(1, targetHeight)
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
        opacity: 0.96
        radius: 2
        border.color: Qt.rgba(1, 1, 1, 0.18)
        border.width: 1
    }

    Item {
        id: contentLayer
        width: Math.max(1, root.targetWidth)
        height: Math.max(1, root.targetHeight)

        Loader {
            id: loader
            objectName: "qbarPopupLoader"
            anchors.fill: parent
            source: contentSource
            asynchronous: false

            onLoaded: {
                if (!item) {
                    return
                }
                for (var key in popupData) {
                    if (Object.prototype.hasOwnProperty.call(popupData, key)) {
                        item[key] = popupData[key]
                    }
                }
            }
        }
    }
}
