import QtQuick
import QtCore
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast
import "qrc:/qbar/Format.js" as Format

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "memory"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    // Graph/swap sub-parts, styled via standard-CSS `#memory.graph { ... }` /
    // `#memory.swap { ... }` selectors.
    readonly property var graphStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "graph") : ({})
    readonly property var swapStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "swap") : ({})

    readonly property color graphBackground: graphStyle["background-color"]
        ? cssTheme.parseColor(graphStyle["background-color"])
        : "transparent"
    readonly property color effectiveGraphBackground: Contrast.effectiveBackground(graphBackground, cssTheme, theme.background)
    readonly property color labelBackground: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#35502e"
    readonly property color labelColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground

    property int usage: cpuModel ? cpuModel.memoryUsage : 0
    property var history: cpuModel ? cpuModel.memoryUsageHistory : []
    property var swapHistory: cpuModel ? cpuModel.swapUsageHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(graphStyle["width"], 22)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    readonly property var parts: memoryConfig && memoryConfig.format ? memoryConfig.format : ["cycle"]
    // The graph is always shown; `format` lists only the value parts beside it.
    readonly property var valueParts: root.parts.filter(function(part) { return part !== "graph" })
    readonly property string labelText: memoryConfig && memoryConfig.text ? memoryConfig.text : "mem"
    readonly property var cycleModes: ["text", "percentage", "absolute", "used", "none"]
    property int cycleIndex: 0

    // Persist the wheel-cycled mode across restarts (like the Clock's format).
    Settings {
        id: cycleSettings
        category: "Memory"
        property int cycleIndex: 0
    }
    onCycleIndexChanged: cycleSettings.cycleIndex = cycleIndex
    property int preferredWidth: configuredWidth > 0 ? configuredWidth : Math.ceil(contentRow.implicitWidth)
    property bool tooltipHovered: false
    property int popupColumns: cpuModel ? (cpuModel.coreCount > 24 ? 6 : (cpuModel.coreCount > 4 ? 4 : 2)) : 2
    property int popupHeaderHeight: 188
    property int popupProcessHeight: 96
    property int popupTileHeight: 84
    property int popupTileSpacing: 8
    property int popupVerticalSpacing: 10
    property int popupOuterMargins: 24
    property int popupGridRows: cpuModel ? Math.max(1, Math.ceil(cpuModel.coreCount / popupColumns)) : 1
    property int popupHeight: popupOuterMargins
        + popupHeaderHeight
        + popupVerticalSpacing
        + popupProcessHeight
        + popupVerticalSpacing
        + (popupGridRows * popupTileHeight + Math.max(0, popupGridRows - 1) * popupTileSpacing)

    signal preferredWidthUpdated(int width)

    function cssPixels(value, fallback) {
        var parsed = parseInt(value)
        return isNaN(parsed) ? fallback : parsed
    }

    function valueForMode(mode) {
        if (mode === "text") return root.labelText
        if (mode === "percentage") return root.usage + "%"
        if (mode === "absolute") {
            return Format.humanizeBytes(cpuModel ? cpuModel.memoryUsedBytes : 0)
                + " / " + Format.humanizeBytes(cpuModel ? cpuModel.memoryTotalBytes : 0)
        }
        if (mode === "used") {
            return Format.humanizeBytes(cpuModel ? cpuModel.memoryUsedBytes : 0)
        }
        return ""
    }

    function valueForPart(part) {
        if (part === "cycle") return root.valueForMode(root.cycleModes[root.cycleIndex])
        return root.valueForMode(part)
    }

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: {
        cycleIndex = ((cycleSettings.cycleIndex % cycleModes.length) + cycleModes.length) % cycleModes.length
        preferredWidthUpdated(preferredWidth)
    }

    QBar.Popup {
        id: memoryPopup
        name: "memory"
        anchorItem: root
        source: "qrc:/popups/CPUPopup.qml"
        payload: ({ cpu: cpuModel, popupMode: "memory", coreColumns: root.popupColumns })
        popupWidth: 640
        popupHeight: root.popupHeight
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.usage + "% memory usage"
            + (cpuModel && cpuModel.memoryTotalBytes > 0
                ? " (" + Format.humanizeBytes(cpuModel.memoryUsedBytes) + " / " + Format.humanizeBytes(cpuModel.memoryTotalBytes) + ")"
                : "")
        side: "auto"
    }

    Component {
        id: textCell
        QBar.CssRect {
            cssId: "memory"
            defaultColor: "#35502e"
            readonly property string cellText: parent ? parent.cellText : ""
            implicitWidth: Math.max(1, cellLabel.implicitWidth + root.labelPadding)
            height: root.height

            Text {
                id: cellLabel
                anchors.centerIn: parent
                color: root.labelColor
                font.family: cssStyle["font-family"] || theme.fontFamily
                font.pointSize: theme.fontSize
                text: cellText
            }
        }
    }

    Component {
        id: graphCell
        QBar.CssRect {
            cssId: "memory"
            cssPart: "graph"
            implicitWidth: root.graphWidth
            height: root.height

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
                        root.swapStyle["fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22),
                        root.swapStyle["color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                    drawSeries(memPoints,
                        root.graphStyle["fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22),
                        root.graphStyle["color"] || Contrast.contrastColor(root.effectiveGraphBackground))
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
    }

    Row {
        id: contentRow
        height: theme.height
        spacing: 0

        Repeater {
            model: root.valueParts
            delegate: Loader {
                required property var modelData
                readonly property string cellText: root.valueForPart(modelData)
                height: root.height
                visible: cellText.length > 0
                sourceComponent: textCell
            }
        }

        Loader {
            height: root.height
            sourceComponent: graphCell
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: memoryPopup.toggle()
        onWheel: function(wheel) {
            var n = root.cycleModes.length
            root.cycleIndex = (root.cycleIndex + (wheel.angleDelta.y > 0 ? -1 : 1) + n) % n
        }
    }
}
