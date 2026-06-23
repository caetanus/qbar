import QtQuick
import QtCore
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast
import "qrc:/qbar/Format.js" as Format

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "cpu"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    // Graph sub-part, styled via the standard-CSS `#cpu.graph { background-color;
    // color; fill; width }` selector.
    readonly property var graphStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "graph") : ({})

    readonly property color graphBackground: graphStyle["background-color"]
        ? cssTheme.parseColor(graphStyle["background-color"])
        : "transparent"
    readonly property color effectiveGraphBackground: Contrast.effectiveBackground(graphBackground, cssTheme, theme.background)
    readonly property color labelBackground: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#35414a"
    readonly property color labelColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground

    property int usage: cpuModel ? cpuModel.usage : 0
    property var history: cpuModel ? cpuModel.usageHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(graphStyle["width"], 22)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    // Composable display parts (config: cpu.format / cpu.text). Default keeps the
    // historical "percentage + graph" look.
    readonly property var parts: cpuConfig && cpuConfig.format ? cpuConfig.format : ["cycle"]
    // The graph is always shown; `format` lists only the value parts beside it
    // ("graph" in the list is ignored). An empty list = graph only.
    readonly property var valueParts: root.parts.filter(function(part) { return part !== "graph" })
    readonly property string labelText: cpuConfig && cpuConfig.text ? cpuConfig.text : "cpu"
    // The "cycle" part steps through these modes on each wheel tick.
    readonly property var cycleModes: ["text", "percentage", "clock", "none"]
    property int cycleIndex: 0

    // Persist the wheel-cycled mode across restarts (like the Clock's format).
    Settings {
        id: cycleSettings
        category: "CPU"
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
        if (mode === "clock") return Format.humanizeClock(cpuModel ? cpuModel.clockMhz : 0)
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
        id: cpuPopup
        name: "cpu"
        anchorItem: root
        source: "qrc:/popups/CPUPopup.qml"
        payload: ({ cpu: cpuModel, coreColumns: root.popupColumns })
        popupWidth: 640
        popupHeight: root.popupHeight
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.usage + "% cpu usage, load avg " + (cpuModel ? cpuModel.loadAverage1.toFixed(2) : "0.00")
            + (cpuModel && cpuModel.clockMhz > 0 ? ", " + Format.humanizeClock(cpuModel.clockMhz) : "")
        side: "auto"
    }

    Component {
        id: textCell
        QBar.CssRect {
            cssId: "cpu"
            defaultColor: "#35414a"
            // `parent` is the Loader; read the resolved text from it.
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
            cssId: "cpu"
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

                    var points = root.history
                    if (!points || points.length === 0) {
                        return
                    }

                    ctx.fillStyle = Contrast.contrastFill(root.effectiveGraphBackground, 0.04)
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
                    ctx.fillStyle = root.graphStyle["fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22)
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
                    ctx.strokeStyle = root.graphStyle["color"] || Contrast.contrastColor(root.effectiveGraphBackground)
                    ctx.lineWidth = 1.5
                    ctx.lineJoin = "round"
                    ctx.lineCap = "round"
                    ctx.stroke()
                }

                Connections {
                    target: cpuModel
                    function onUsageChanged() { graph.requestPaint() }
                    function onUsageHistoryChanged() { graph.requestPaint() }
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
        onClicked: cpuPopup.toggle()
        onWheel: function(wheel) {
            var n = root.cycleModes.length
            root.cycleIndex = (root.cycleIndex + (wheel.angleDelta.y > 0 ? -1 : 1) + n) % n
        }
    }
}
