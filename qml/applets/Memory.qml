import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "memory"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    readonly property color graphBackground: cssStyle["graph-background"]
        ? cssTheme.parseColor(cssStyle["graph-background"])
        : "transparent"
    readonly property color effectiveGraphBackground: Contrast.effectiveBackground(graphBackground, cssTheme, theme.background)

    property int usage: cpuModel ? cpuModel.memoryUsage : 0
    property var history: cpuModel ? cpuModel.memoryUsageHistory : []
    property var swapHistory: cpuModel ? cpuModel.swapUsageHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(cssStyle["graph-width"], 22)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    property int preferredWidth: configuredWidth > 0
        ? configuredWidth
        : Math.ceil(usageLabel.implicitWidth + labelPadding + graphWidth)
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)

    function cssPixels(value, fallback) {
        var parsed = parseInt(value)
        return isNaN(parsed) ? fallback : parsed
    }

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Popup {
        id: memoryPopup
        anchorItem: root
        source: "qrc:/popups/CPUPopup.qml"
        payload: ({ cpu: cpuModel, popupMode: "memory", coreColumns: cpuModel ? (cpuModel.coreCount > 32 ? 6 : (cpuModel.coreCount > 16 ? 4 : (cpuModel.coreCount > 8 ? 3 : 2))) : 2 })
        popupWidth: 520
        popupHeight: cpuModel ? (24 + 82 + 10 + 14 + 10 + (Math.max(1, Math.ceil(cpuModel.coreCount / (cpuModel.coreCount > 32 ? 6 : (cpuModel.coreCount > 16 ? 4 : (cpuModel.coreCount > 8 ? 3 : 2))))) * 96 + Math.max(0, Math.ceil(cpuModel.coreCount / (cpuModel.coreCount > 32 ? 6 : (cpuModel.coreCount > 16 ? 4 : (cpuModel.coreCount > 8 ? 3 : 2)))) - 1) * 8)) : 320
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.usage + "% memory usage"
        side: "auto"
    }

    Row {
        id: contentRow
        height: theme.height
        spacing: 0

        Rectangle {
            width: Math.max(1, root.preferredWidth - graphBlock.width)
            height: theme.height
            radius: 0
            color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#35502e"

            Text {
                id: usageLabel
                anchors.centerIn: parent
                color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
                font.family: cssStyle["font-family"] || theme.fontFamily
                font.pointSize: theme.fontSize
                text: root.usage + "%"
            }
        }

        Rectangle {
            id: graphBlock
            width: root.graphWidth
            height: theme.height
            radius: 0
            color: root.graphBackground

            Canvas {
                id: graph
                anchors.fill: parent
                anchors.margins: 4

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var swapPoints = root.swapHistory
                    var memPoints = root.history
                    if ((!swapPoints || swapPoints.length === 0) && (!memPoints || memPoints.length === 0)) {
                        return
                    }

                    function drawSeries(points, fillColor, strokeColor) {
                        if (!points || points.length === 0) {
                            return
                        }

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
                        ctx.fillStyle = fillColor
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
                        ctx.strokeStyle = strokeColor
                        ctx.lineWidth = 1.5
                        ctx.lineJoin = "round"
                        ctx.lineCap = "round"
                        ctx.stroke()
                    }

                    drawSeries(swapPoints,
                        cssStyle["swap-fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22),
                        cssStyle["swap-color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                    drawSeries(memPoints,
                        cssStyle["graph-fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22),
                        cssStyle["graph-color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                }
            }

            Connections {
                target: cpuModel
                function onMemoryUsageChanged() { graph.requestPaint() }
                function onMemoryUsageHistoryChanged() { graph.requestPaint() }
                function onMemoryStatsChanged() { graph.requestPaint() }
            }

            Connections {
                target: root
                function onCssStyleChanged() { graph.requestPaint() }
            }

            Component.onCompleted: graph.requestPaint()
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: memoryPopup.toggle()
    }
}
