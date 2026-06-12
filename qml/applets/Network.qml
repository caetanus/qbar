import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    property double downloadRateBytesPerSecond: networkModel ? networkModel.downloadRateBytesPerSecond : 0
    property double uploadRateBytesPerSecond: networkModel ? networkModel.uploadRateBytesPerSecond : 0
    property double totalRateBytesPerSecond: networkModel ? networkModel.totalRateBytesPerSecond : 0
    property var downloadHistory: networkModel ? networkModel.downloadRateHistory : []
    property var uploadHistory: networkModel ? networkModel.uploadRateHistory : []
    property int preferredWidth: Math.ceil(rateLabel.implicitWidth + 58)
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)

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
            width: rateLabel.implicitWidth + 10
            height: theme.height
            radius: 0
            color: "#3a3410"

            Text {
                id: rateLabel
                anchors.centerIn: parent
                color: theme.foreground
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                text: root.formatRate(root.totalRateBytesPerSecond)
            }
        }

        Rectangle {
            width: 10
            height: theme.height
            radius: 0
            color: "transparent"

            Text {
                anchors.centerIn: parent
                color: root.downloadRateBytesPerSecond >= root.uploadRateBytesPerSecond ? "#eab308" : "#5b5241"
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                font.bold: true
                text: "↓"
            }
        }

        Rectangle {
            width: 10
            height: theme.height
            radius: 0
            color: "transparent"

            Text {
                anchors.centerIn: parent
                color: root.uploadRateBytesPerSecond > root.downloadRateBytesPerSecond ? "#f59e0b" : "#5b5241"
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                font.bold: true
                text: "↑"
            }
        }

        Rectangle {
            width: 46
            height: theme.height
            radius: 0
            color: "#24303a"

            Canvas {
                id: trafficGraph
                anchors.fill: parent
                anchors.margins: 4

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    root.drawSeries(ctx, root.downloadHistory, width, height, "rgba(234, 179, 8, 0.22)", "#eab308")
                    root.drawSeries(ctx, root.uploadHistory, width, height, "rgba(161, 98, 7, 0.26)", "#a16207")
                }
            }

            Connections {
                target: networkModel
                function onStatsChanged() { trafficGraph.requestPaint() }
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
