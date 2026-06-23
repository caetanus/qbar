import QtQuick
import QBar 1.0
import QtCore
import "qrc:/qbar" as Chrome
import "qrc:/qbar/Contrast.js" as Contrast
import "qrc:/qbar/Format.js" as Format

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property string cssId: "network-io"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    // Standard-CSS sub-parts: the sparkline graph (#network-io.graph), its two series
    // (#network-io.download / .upload), and the direction arrows — #network-io.arrow is
    // the shared base (width + idle color via :inactive), .arrowDown/.arrowUp carry each
    // direction's active color.
    readonly property var graphStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "graph") : ({})
    readonly property var downloadStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "download") : ({})
    readonly property var uploadStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "upload") : ({})
    readonly property var arrowStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "arrow") : ({})
    readonly property var arrowDownStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "arrowDown") : ({})
    readonly property var arrowUpStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "arrowUp") : ({})
    readonly property var arrowInactiveStyle: cssTheme && cssTheme.loaded ? cssTheme.resolvePart(cssId, "arrow", ["inactive"]) : ({})

    readonly property color barBackground: Contrast.barBackground(cssTheme, theme.background)
    readonly property color graphBackground: graphStyle["background-color"]
        ? cssTheme.parseColor(graphStyle["background-color"])
        : "transparent"
    readonly property color effectiveGraphBackground: Contrast.effectiveBackground(graphBackground, cssTheme, theme.background)
    // The graph cell's panel fill. Order: an explicit #network-io.graph background-color wins;
    // otherwise mirror the theme's #cpu.graph panel so this graph matches the CPU/Memory graphs
    // (themes style #cpu.graph/#memory.graph but rarely #network-io.graph, which used to leave
    // this one washed-out beside them). Final fallback, for themes that style no graph panel at
    // all, is the same subtle contrast tint CPU paints inside its sparkline.
    readonly property color graphCellBackground: {
        if (graphStyle["background-color"])
            return cssTheme.parseColor(graphStyle["background-color"])
        var cpuGraph = (cssTheme && cssTheme.loaded) ? cssTheme.resolvePart("cpu", "graph") : ({})
        if (cpuGraph && cpuGraph["background-color"])
            return cssTheme.parseColor(cpuGraph["background-color"])
        return Contrast.contrastFill(effectiveGraphBackground, 0.04)
    }
    readonly property color labelBackground: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "#3a3410"
    readonly property color labelColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground

    readonly property color downloadLineColor: downloadStyle["color"]
        ? cssTheme.parseColor(downloadStyle["color"]) : Contrast.contrastColor(root.effectiveGraphBackground)
    readonly property color downloadFillColor: downloadStyle["fill"]
        ? cssTheme.parseColor(downloadStyle["fill"])
        : Qt.rgba(downloadLineColor.r, downloadLineColor.g, downloadLineColor.b, 0.22)
    readonly property color uploadLineColor: uploadStyle["color"]
        ? cssTheme.parseColor(uploadStyle["color"]) : Contrast.contrastColor(root.effectiveGraphBackground)
    readonly property color uploadFillColor: uploadStyle["fill"]
        ? cssTheme.parseColor(uploadStyle["fill"])
        : Qt.rgba(uploadLineColor.r, uploadLineColor.g, uploadLineColor.b, 0.26)

    property double downloadRateBytesPerSecond: networkModel ? networkModel.downloadRateBytesPerSecond : 0
    property double uploadRateBytesPerSecond: networkModel ? networkModel.uploadRateBytesPerSecond : 0
    property double totalRateBytesPerSecond: networkModel ? networkModel.totalRateBytesPerSecond : 0
    property var downloadHistory: networkModel ? networkModel.downloadRateHistory : []
    property var uploadHistory: networkModel ? networkModel.uploadRateHistory : []
    property int configuredWidth: cssPixels(cssStyle["width"], 0)
    property int graphWidth: cssPixels(graphStyle["width"], 22)
    property int arrowWidth: cssPixels(arrowStyle["width"], 8)
    property int labelPadding: cssPixels(cssStyle["label-padding"], 10)
    readonly property var parts: networkConfig && networkConfig.format ? networkConfig.format : ["cycle"]
    // The graph (with its direction arrows) is always shown; `format` lists only
    // the value parts beside it.
    readonly property var valueParts: root.parts.filter(function(part) { return part !== "graph" })
    readonly property string labelText: networkConfig && networkConfig.text ? networkConfig.text : "net"
    readonly property var cycleModes: ["text", "absolute", "none"]
    property int cycleIndex: 0

    // Persist the wheel-cycled mode across restarts (like the Clock's format).
    Settings {
        id: cycleSettings
        category: "Network"
        property int cycleIndex: 0
    }
    onCycleIndexChanged: cycleSettings.cycleIndex = cycleIndex
    property int preferredWidth: configuredWidth > 0 ? configuredWidth : Math.ceil(contentRow.implicitWidth)
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
    Component.onCompleted: {
        cycleIndex = ((cycleSettings.cycleIndex % cycleModes.length) + cycleModes.length) % cycleModes.length
        preferredWidthUpdated(preferredWidth)
    }

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

    function valueForMode(mode) {
        if (mode === "text") return root.labelText
        if (mode === "absolute") return root.formatRate(root.totalRateBytesPerSecond)
        return ""
    }

    function valueForPart(part) {
        if (part === "cycle") return root.valueForMode(root.cycleModes[root.cycleIndex])
        return root.valueForMode(part)
    }

    Chrome.Popup {
        id: netPopup
        name: "network"
        anchorItem: root
        source: "qrc:/popups/NetworkPopup.qml"
        payload: ({ net: networkModel, procs: networkProcessModel })
        popupWidth: 380
        popupHeight: 320
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    Chrome.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered && !netPopup.isOpen
        text: "down " + root.formatRate(root.downloadRateBytesPerSecond) + ", up " + root.formatRate(root.uploadRateBytesPerSecond) + ", total " + root.formatRate(root.totalRateBytesPerSecond)
        side: "auto"
    }

    Component {
        id: textCell
        Rectangle {
            readonly property string cellText: parent ? parent.cellText : ""
            // Reserve at least "00.0 M/s" so the label doesn't jitter as the rate changes.
            implicitWidth: Math.max(1, Math.max(cellLabel.implicitWidth, rateMetrics.implicitWidth) + root.labelPadding)
            height: root.height
            color: root.labelBackground

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
        Row {
            height: root.height
            spacing: 0

            Rectangle {
                width: root.arrowWidth
                height: root.height
                color: root.graphCellBackground
                Text {
                    anchors.centerIn: parent
                    color: root.downloadRateBytesPerSecond >= root.uploadRateBytesPerSecond
                        ? (root.arrowDownStyle["color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                        : (root.arrowInactiveStyle["color"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.4))
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    font.bold: true
                    text: "↓"
                }
            }

            Rectangle {
                width: root.arrowWidth
                height: root.height
                color: root.graphCellBackground
                Text {
                    anchors.centerIn: parent
                    color: root.uploadRateBytesPerSecond > root.downloadRateBytesPerSecond
                        ? (root.arrowUpStyle["color"] || Contrast.contrastColor(root.effectiveGraphBackground))
                        : (root.arrowInactiveStyle["color"] || Contrast.contrastFill(root.effectiveGraphBackground, 0.4))
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    font.bold: true
                    text: "↑"
                }
            }

            Rectangle {
                width: root.graphWidth
                height: root.height
                color: root.graphCellBackground

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
        acceptedButtons: Qt.LeftButton
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: netPopup.toggle()
        onWheel: function(wheel) {
            var n = root.cycleModes.length
            root.cycleIndex = (root.cycleIndex + (wheel.angleDelta.y > 0 ? -1 : 1) + n) % n
        }
    }
}
