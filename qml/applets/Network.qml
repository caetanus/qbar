import QtQuick
import QBar 1.0
import "qrc:/qbar" as Chrome
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

    readonly property color downloadLineColor: cssStyle["download-color"]
        ? cssTheme.parseColor(cssStyle["download-color"]) : Contrast.contrastColor(root.effectiveGraphBackground)
    readonly property color downloadFillColor: cssStyle["download-fill"]
        ? cssTheme.parseColor(cssStyle["download-fill"])
        : Qt.rgba(downloadLineColor.r, downloadLineColor.g, downloadLineColor.b, 0.22)
    readonly property color uploadLineColor: cssStyle["upload-color"]
        ? cssTheme.parseColor(cssStyle["upload-color"]) : Contrast.contrastColor(root.effectiveGraphBackground)
    readonly property color uploadFillColor: cssStyle["upload-fill"]
        ? cssTheme.parseColor(cssStyle["upload-fill"])
        : Qt.rgba(uploadLineColor.r, uploadLineColor.g, uploadLineColor.b, 0.26)

    property double downloadRateBytesPerSecond: networkModel ? networkModel.downloadRateBytesPerSecond : 0
    property double uploadRateBytesPerSecond: networkModel ? networkModel.uploadRateBytesPerSecond : 0
    property double totalRateBytesPerSecond: networkModel ? networkModel.totalRateBytesPerSecond : 0
    property var downloadHistory: networkModel ? networkModel.downloadRateHistory : []
    property var uploadHistory: networkModel ? networkModel.uploadRateHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(cssStyle["graph-width"], 22)
    property int arrowWidth: cssPixels(cssStyle["arrow-width"], 8)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    // Reserve at least the width of "00.0 M/s" so the label doesn't jitter as the
    // rate changes; wider values (e.g. "100 M/s") still grow past it.
    property real labelWidth: Math.max(rateLabel.implicitWidth, rateMetrics.implicitWidth)
    property int preferredWidth: configuredWidth > 0
        ? configuredWidth
        : Math.ceil(labelWidth + labelPadding + (arrowWidth * 2) + graphWidth)
    property bool tooltipHovered: false

    Text {
        id: rateMetrics
        visible: false
        text: "00.0 M/s"
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
    }

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


    Chrome.Tooltip {
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

            // GPU scene-graph graphs (Sparkline) — no Canvas raster/texture upload
            // per tick. Two overlaid series: download and upload.
            Sparkline {
                anchors.fill: parent
                anchors.margins: 4
                values: root.downloadHistory
                lineWidth: 1.5
                lineColor: root.downloadLineColor
                fillColor: root.downloadFillColor
            }

            Sparkline {
                anchors.fill: parent
                anchors.margins: 4
                values: root.uploadHistory
                lineWidth: 1.5
                lineColor: root.uploadLineColor
                fillColor: root.uploadFillColor
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
