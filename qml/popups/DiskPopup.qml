import QtQuick

Item {
    id: root
    width: 420
    implicitWidth: 420
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight

    property var disk: null
    property var mounts: disk ? disk.mounts : []
    property int columns: mounts.length > 1 ? 2 : 1
    property int tileHeight: 86
    property color textColor: theme.foreground
    property color textSoft: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.78)
    property color panelBackground: Qt.rgba(1, 1, 1, 0.07)
    property color panelBorder: Qt.rgba(1, 1, 1, 0.14)
    property color usedColor: "#7dd3fc"
    property color freeColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.14)

    function percentValue(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return 0
        }
        return Math.max(0, Math.min(100, Math.round(number)))
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
            text: "disk usage"
        }

        Grid {
            id: mountGrid
            width: parent.width
            columns: root.columns
            rowSpacing: 8
            columnSpacing: 8

            Repeater {
                model: root.mounts

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
