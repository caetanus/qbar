import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "network-io"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    readonly property color barBackground: Contrast.barBackground(cssTheme, theme.background)
    readonly property color graphBackground: cssStyle["graph-background"]
        ? cssTheme.parseColor(cssStyle["graph-background"])
        : "transparent"
    readonly property color effectiveGraphBackground: Contrast.effectiveBackground(graphBackground, cssTheme, theme.background)

    property double downloadRateBytesPerSecond: networkModel ? networkModel.downloadRateBytesPerSecond : 0
    property double uploadRateBytesPerSecond: networkModel ? networkModel.uploadRateBytesPerSecond : 0
    property double totalRateBytesPerSecond: networkModel ? networkModel.totalRateBytesPerSecond : 0
    property var downloadHistory: networkModel ? networkModel.downloadRateHistory : []
    property var uploadHistory: networkModel ? networkModel.uploadRateHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(cssStyle["graph-width"], 22)
    property int arrowWidth: cssPixels(cssStyle["arrow-width"], 8)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    property int preferredWidth: configuredWidth > 0
        ? configuredWidth
        : Math.ceil(rateLabel.implicitWidth + labelPadding + (arrowWidth * 2) + graphWidth)
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)

    function cssPixels(value, fallback) {
        var parsed = parseInt(value)
        return isNaN(parsed) ? fallback : parsed
    }

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function formatRate(bytesPerSecond) {
        var value = Number(bytesPerSecond)
        if (isNaN(value) || value <= 0) {
            return "0.0 KiB/s"
        }

        var kib = value / 1024.0
        if (kib < 30.0) {
            return kib.toFixed(1).replace(/\.0$/, "") + " K/s"
        }

        var mib = kib / 1024.0
        return mib.toFixed(1).replace(/\.0$/, "") + " M/s"
    }

    function maxHistoryValue(points) {
        var maxValue = 0
        if (!points) {
            return maxValue
        }
        for (var i = 0; i < points.length; ++i) {
            var sample = Number(points[i])
            if (!isNaN(sample) && sample > maxValue) {
                maxValue = sample
            }
        }
        return maxValue
    }

    function drawSeries(ctx, points, width, height, fillColor, strokeColor) {
        var maxValue = root.maxHistoryValue(points)
        if (!points || points.length === 0 || maxValue <= 0) {
            return
        }

        ctx.beginPath()
        ctx.moveTo(0, height)
        for (var i = 0; i < points.length; ++i) {
            var sample = Number(points[i])
            if (isNaN(sample)) {
                sample = 0
            }
            var normalized = Math.max(0, Math.min(1, sample / maxValue))
            var x = points.length === 1 ? width - 1 : (i * (width - 1)) / (points.length - 1)
            var y = height - Math.max(1, normalized * (height - 2)) - 1
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
            var normalizedValue = Math.max(0, Math.min(1, value / maxValue))
            var px = points.length === 1 ? width - 1 : (j * (width - 1)) / (points.length - 1)
            var py = height - Math.max(1, normalizedValue * (height - 2)) - 1
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

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: "down " + root.formatRate(root.downloadRateBytesPerSecond) + ", up " + root.formatRate(root.uploadRateBytesPerSecond) + ", total " + root.formatRate(root.totalRateBytesPerSecond)
        side: "auto"
    }

    Row {
        height: theme.height
        spacing: 0

        Rectangle {
            width: Math.max(1, root.preferredWidth - root.graphWidth - (root.arrowWidth * 2))
            height: theme.height
            radius: 0
            color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#3a3410"

            Text {
                id: rateLabel
                anchors.centerIn: parent
                color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
                font.family: cssStyle["font-family"] || theme.fontFamily
                font.pointSize: theme.fontSize
                text: root.formatRate(root.totalRateBytesPerSecond)
            }
        }

        Rectangle {
            width: root.arrowWidth
            height: theme.height
            radius: 0
            color: "transparent"

            Text {
                anchors.centerIn: parent
                color: root.downloadRateBytesPerSecond >= root.uploadRateBytesPerSecond
                    ? (cssStyle["download-color"] || Contrast.contrastColor(root.barBackground))
                    : (cssStyle["inactive-color"] || Contrast.contrastFill(root.barBackground, 0.4))
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                font.bold: true
                text: "↓"
            }
        }

        Rectangle {
            width: root.arrowWidth
            height: theme.height
            radius: 0
            color: "transparent"

            Text {
                anchors.centerIn: parent
                color: root.uploadRateBytesPerSecond > root.downloadRateBytesPerSecond
                    ? (cssStyle["upload-color"] || Contrast.contrastColor(root.barBackground))
                    : (cssStyle["inactive-color"] || Contrast.contrastFill(root.barBackground, 0.4))
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                font.bold: true
                text: "↑"
            }
        }

        Rectangle {
            width: root.graphWidth
            height: theme.height
            radius: 0
            color: root.graphBackground

            Canvas {
                id: trafficGraph
                anchors.fill: parent
                anchors.margins: 4

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    root.drawSeries(ctx, root.downloadHistory, width, height,
                        cssStyle["download-fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.22),
                        cssStyle["download-color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                    root.drawSeries(ctx, root.uploadHistory, width, height,
                        cssStyle["upload-fill"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.26),
                        cssStyle["upload-color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                }
            }

            Connections {
                target: networkModel
                function onStatsChanged() { trafficGraph.requestPaint() }
            }

            Connections {
                target: root
                function onCssStyleChanged() { trafficGraph.requestPaint() }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
    }
}
