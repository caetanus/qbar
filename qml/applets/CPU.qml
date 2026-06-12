import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    property int usage: cpuModel ? cpuModel.usage : 0
    property var history: cpuModel ? cpuModel.usageHistory : []
    property int preferredWidth: Math.ceil(usageLabel.implicitWidth + 28)
    property bool tooltipHovered: false
    property int popupColumns: cpuModel ? (cpuModel.coreCount > 32 ? 6 : (cpuModel.coreCount > 16 ? 4 : (cpuModel.coreCount > 8 ? 3 : 2))) : 2
    property int popupHeaderHeight: 82
    property int popupMetricHeight: 0
    property int popupMemoryHeight: 14
    property int popupTileHeight: 96
    property int popupTileSpacing: 8
    property int popupVerticalSpacing: 10
    property int popupOuterMargins: 24
    property int popupGridRows: cpuModel ? Math.max(1, Math.ceil(cpuModel.coreCount / popupColumns)) : 1
    property int popupHeight: popupOuterMargins
        + popupHeaderHeight
        + popupVerticalSpacing
        + popupMetricHeight
        + popupVerticalSpacing
        + popupMemoryHeight
        + popupVerticalSpacing
        + (popupGridRows * popupTileHeight + Math.max(0, popupGridRows - 1) * popupTileSpacing)

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Popup {
        id: cpuPopup
        anchorItem: root
        source: "qrc:/popups/CPUPopup.qml"
        payload: ({ cpu: cpuModel, coreColumns: root.popupColumns })
        popupWidth: 520
        popupHeight: root.popupHeight
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.usage + "% cpu usage, load avg " + (cpuModel ? cpuModel.loadAverage1.toFixed(2) : "0.00")
        side: "auto"
    }

    Row {
        id: contentRow
        height: theme.height
        spacing: 0

        Rectangle {
            width: usageLabel.implicitWidth + 10
            height: theme.height
            radius: 0
            color: "#35414a"

            Text {
                id: usageLabel
                anchors.centerIn: parent
                color: theme.foreground
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                text: root.usage + "%"
            }
        }

        Rectangle {
            id: graphBlock
            width: 22
            height: theme.height
            radius: 0
            color: "#24303a"

            Canvas {
                id: graph
                anchors.fill: parent
                anchors.margins: 4

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var points = root.history
                    if (!points || points.length === 0) {
                        return
                    }

                    ctx.fillStyle = "rgba(255, 255, 255, 0.04)"
                    ctx.fillRect(0, 0, width, height)

                    ctx.beginPath()
                    ctx.moveTo(0, height)
                    for (var i = 0; i < points.length; ++i) {
                        var sample = Number(points[i])
                        if (isNaN(sample)) {
                            sample = 0
                        }
                        var x = points.length === 1 ? width - 1 : (i * (width - 1)) / (points.length - 1)
                        var y = height - Math.max(1, (sample / 100.0) * (height - 2)) - 1
                        ctx.lineTo(x, y)
                    }
                    ctx.lineTo(width - 1, height)
                    ctx.closePath()
                    ctx.fillStyle = "rgba(99, 179, 237, 0.22)"
                    ctx.fill()

                    ctx.beginPath()
                    for (var j = 0; j < points.length; ++j) {
                        var value = Number(points[j])
                        if (isNaN(value)) {
                            value = 0
                        }
                        var px = points.length === 1 ? width - 1 : (j * (width - 1)) / (points.length - 1)
                        var py = height - Math.max(1, (value / 100.0) * (height - 2)) - 1
                        if (j === 0) {
                            ctx.moveTo(px, py)
                        } else {
                            ctx.lineTo(px, py)
                        }
                    }
                    ctx.strokeStyle = "#63b3ed"
                    ctx.lineWidth = 1.5
                    ctx.lineJoin = "round"
                    ctx.lineCap = "round"
                    ctx.stroke()
                }
            }

            Connections {
                target: cpuModel
                function onUsageChanged() { graph.requestPaint() }
                function onUsageHistoryChanged() { graph.requestPaint() }
            }

            Component.onCompleted: graph.requestPaint()
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: cpuPopup.toggle()
    }
}
