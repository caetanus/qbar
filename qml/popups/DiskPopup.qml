import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 420
    implicitWidth: 420
    implicitHeight: 300
    height: implicitHeight

    property var disk: null
    property var mounts: disk ? disk.mounts : []
    property var sortedMounts: sortMounts(mounts)
    property int columns: mounts.length > 1 ? 2 : 1
    property int tileHeight: 86
    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    property color textColor: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    property color textSoft: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.78)
    property color panelBackground: Qt.rgba(1, 1, 1, 0.07)
    property color panelBorder: Qt.rgba(1, 1, 1, 0.14)
    property color usedColor: "#7dd3fc"
    property color freeColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.14)
    property int bottomPadding: Math.ceil(theme.fontSize)

    function percentValue(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return 0
        }
        return Math.max(0, Math.min(100, Math.round(number)))
    }

    function mountPriority(path) {
        var value = String(path || "")
        if (value === "/") {
            return 0
        }
        if (value === "/home" || value.indexOf("/home/") === 0) {
            return 1
        }
        if (value.indexOf("/mnt/") === 0 || value.indexOf("/media/") === 0 || value.indexOf("/run/media/") === 0) {
            return 2
        }
        if (value.indexOf("/var/") === 0 || value.indexOf("/boot") === 0 || value.indexOf("/snap") === 0) {
            return 5
        }
        return 3
    }

    function sortMounts(list) {
        var copy = []
        for (var i = 0; list && i < list.length; ++i) {
            copy.push(list[i])
        }
        copy.sort(function(a, b) {
            var pa = mountPriority(a.path)
            var pb = mountPriority(b.path)
            if (pa !== pb) {
                return pa - pb
            }
            return String(a.path || "").localeCompare(String(b.path || ""))
        })
        return copy
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: root.panelBorder
        border.width: 1
        radius: 4
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 10

        Text {
            width: parent.width
            color: root.textColor
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
            font.bold: true
            text: qsTr("disk usage")
        }

        Flickable {
            id: scrollArea
            width: parent.width
            height: root.height - y - root.bottomPadding - 8
            contentWidth: width
            contentHeight: mountGrid.implicitHeight + root.bottomPadding
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            rightMargin: diskScrollBar.visible ? 8 : 0

            ScrollBar.vertical: ScrollBar {
                id: diskScrollBar
                policy: scrollArea.contentHeight > scrollArea.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 5
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 2
                    color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.46)
                }
                background: Rectangle {
                    implicitWidth: 5
                    radius: 2
                    color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.10)
                }
            }

            Grid {
                id: mountGrid
                width: parent.width - scrollArea.rightMargin
                columns: root.columns
                rowSpacing: 8
                columnSpacing: 8

                Repeater {
                    model: root.sortedMounts

                    delegate: Rectangle {
                        id: tile
                        width: Math.floor((mountGrid.width - mountGrid.columnSpacing * (root.columns - 1)) / root.columns)
                        height: root.tileHeight
                        radius: 4
                        color: root.panelBackground
                        border.color: root.panelBorder
                        border.width: 1

                        readonly property int usage: root.percentValue(modelData.percent)

                        Row {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 10

                            Item {
                                width: 54
                                height: 54
                                anchors.verticalCenter: parent.verticalCenter

                                Canvas {
                                    id: pie
                                    anchors.fill: parent

                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)

                                        var cx = width / 2
                                        var cy = height / 2
                                        var radius = Math.min(width, height) / 2 - 2
                                        var start = -Math.PI / 2
                                        var end = start + (Math.PI * 2 * tile.usage / 100)

                                        ctx.beginPath()
                                        ctx.moveTo(cx, cy)
                                        ctx.arc(cx, cy, radius, 0, Math.PI * 2, false)
                                        ctx.closePath()
                                        ctx.fillStyle = root.freeColor
                                        ctx.fill()

                                        if (tile.usage > 0) {
                                            ctx.beginPath()
                                            ctx.moveTo(cx, cy)
                                            ctx.arc(cx, cy, radius, start, end, false)
                                            ctx.closePath()
                                            ctx.fillStyle = root.usedColor
                                            ctx.fill()
                                        }

                                        ctx.beginPath()
                                        ctx.arc(cx, cy, radius, 0, Math.PI * 2, false)
                                        ctx.strokeStyle = Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.22)
                                        ctx.lineWidth = 1
                                        ctx.stroke()
                                    }
                                }

                                Connections {
                                    target: tile
                                    function onUsageChanged() { pie.requestPaint() }
                                }

                                Component.onCompleted: pie.requestPaint()
                            }

                            Column {
                                width: parent.width - x
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 5

                                Text {
                                    width: parent.width
                                    color: root.textColor
                                    font.family: theme.fontFamily
                                    font.pointSize: theme.fontSize
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                    text: modelData.path || modelData.name || "--"
                                }

                                Text {
                                    width: parent.width
                                    color: root.textSoft
                                    font.family: theme.fontFamily
                                    font.pointSize: theme.fontSize
                                    text: tile.usage + "% usage"
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        z: 10
        color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.28)
    }
}
