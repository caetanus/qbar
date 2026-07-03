import QtQuick
import QBar 1.0
import "qrc:/qbar" as Chrome
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    width: 640
    implicitWidth: 640
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight

    property var cpu: null
    property string popupMode: (typeof popupData !== "undefined" && popupData && popupData.popupMode !== undefined) ? String(popupData.popupMode) : "cpu"
    property int coreColumns: 4
    property int coreTileHeight: 84
    property int coreSpacing: 8

    readonly property bool memoryMode: popupMode === "memory"
    readonly property color panelBackground: Qt.rgba(1, 1, 1, 0.075)
    readonly property color panelBackgroundAlt: Qt.rgba(1, 1, 1, 0.105)
    readonly property color panelBorder: Qt.rgba(1, 1, 1, 0.16)
    readonly property color panelGraphBackground: Qt.rgba(0, 0, 0, 0.28)
    readonly property color panelGridLine: Qt.rgba(1, 1, 1, 0.09)
    readonly property color cpuBlue: "#7dd3fc"
    readonly property color cpuHot: "#fda4af"
    readonly property color memoryGreen: "#bef264"
    readonly property color memoryCache: "#7dd3fc"  // reclaimable cache/buffers segment
    readonly property color swapPink: "#f9a8d4"
    readonly property color loadAmber: "#fcd34d"
    readonly property real loadScale: Math.max(1, cpu ? cpu.coreCount : 1)
    readonly property bool cssAvailable: typeof cssTheme !== "undefined" && cssTheme && cssTheme.loaded
    readonly property var popupStyle: cssAvailable ? cssTheme.resolve("popup") : ({})
    readonly property var cpuPopupStyle: cssAvailable ? cssTheme.resolve("cpu-popup") : ({})
    readonly property color fallbackPopupBackground: "#1c1d24"
    readonly property color popupBackground: cssAvailable
        ? Contrast.styleBackgroundColor(popupStyle, cssTheme, fallbackPopupBackground)
        : fallbackPopupBackground
    readonly property color panelEffectiveBackground: Contrast.blendOver(panelBackground, popupBackground)
    readonly property bool hasCssPopupForeground: cssAvailable && (cpuPopupStyle["color"] || popupStyle["color"])
    readonly property color cssPopupForeground: cssAvailable && cpuPopupStyle["color"]
        ? cssTheme.parseColor(cpuPopupStyle["color"])
        : (cssAvailable && popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : "#eef2f7")
    readonly property color fallbackPopupForeground: Contrast.naturalContrastColor(
        colorValue(theme.foreground, "#eef2f7"), panelEffectiveBackground, 4.5)
    readonly property color popupForeground: hasCssPopupForeground ? cssPopupForeground : fallbackPopupForeground
    readonly property color panelText: popupForeground
    readonly property color panelTextSoft: Qt.rgba(popupForeground.r, popupForeground.g, popupForeground.b, 0.92)
    readonly property bool hasCssLabelTextShadow: cssAvailable && (cpuPopupStyle["text-shadow"] || popupStyle["text-shadow"])
    readonly property var cssLabelTextShadow: hasCssLabelTextShadow
        ? cssTheme.parseBoxShadow(cpuPopupStyle["text-shadow"] || popupStyle["text-shadow"] || "") : ({})
    readonly property var automaticLabelTextShadow: Contrast.luminance(panelText) < 0.5
        ? ({ x: 0, y: 1, blur: 1.5, color: Qt.rgba(1, 1, 1, 0.42) })
        : ({ x: 0, y: 1, blur: 2, color: Qt.rgba(0.06, 0.07, 0.12, 0.46) })
    readonly property var labelTextShadow: hasCssLabelTextShadow ? cssLabelTextShadow : automaticLabelTextShadow

    component PopupText: Text {
        layer.enabled: root.labelTextShadow.color !== undefined
        layer.effect: Chrome.CssDropShadow { shadow: root.labelTextShadow }
    }

    function colorValue(value, fallback) {
        if (value === undefined || value === null) {
            return Qt.color(fallback)
        }
        if (typeof value === "string") {
            return Qt.color(value)
        }
        return value
    }

    function clamp01(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return 0
        }
        return Math.max(0, Math.min(1, number))
    }

    function formatPercent(value) {
        if (value === undefined || value === null) {
            return "--"
        }
        return Math.round(Number(value)) + "%"
    }

    function formatLoad(value) {
        if (value === undefined || value === null) {
            return "--"
        }
        return Number(value).toFixed(2)
    }

    function formatMemoryKb(valueKb) {
        if (valueKb === undefined || valueKb === null) {
            return "--"
        }

        var kb = Number(valueKb)
        if (isNaN(kb) || kb <= 0) {
            return "--"
        }

        if (kb >= 1024 * 1024) {
            return (kb / (1024 * 1024)).toFixed(1) + " GiB"
        }

        if (kb >= 1024) {
            return (kb / 1024).toFixed(1) + " MiB"
        }

        return Math.round(kb) + " KiB"
    }

    function processList() {
        if (!cpu) {
            return []
        }
        return memoryMode ? cpu.topMemoryProcesses : cpu.topProcesses
    }

    function processName(item) {
        if (!item) {
            return "--"
        }
        if (item.name !== undefined && String(item.name).length > 0) {
            return String(item.name)
        }
        return "pid " + item.pid
    }

    function processValue(item) {
        if (!item) {
            return "--"
        }
        if (memoryMode) {
            return formatMemoryKb(item.memoryKb)
        }
        return (item.usage !== undefined ? Number(item.usage).toFixed(1) : "0.0") + "%"
    }

    function processWeight(item, list) {
        if (!item) {
            return 0
        }
        if (!memoryMode) {
            return clamp01(Number(item.usage) / 100.0)
        }

        var maxKb = 0
        for (var i = 0; list && i < list.length; ++i) {
            maxKb = Math.max(maxKb, Number(list[i].memoryKb))
        }
        if (maxKb <= 0) {
            return 0
        }
        return clamp01(Number(item.memoryKb) / maxKb)
    }

    function usageColor(value) {
        var percent = Number(value)
        if (isNaN(percent)) {
            return cpuBlue
        }
        if (percent >= 80) {
            return root.cpuHot
        }
        if (percent >= 55) {
            return root.loadAmber
        }
        return cpuBlue
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Qt.rgba(root.panelText.r, root.panelText.g, root.panelText.b, 0.18)
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

        Rectangle {
            width: contentColumn.width
            height: 188
            radius: 4
            color: root.panelBackground
            border.color: root.panelBorder
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 9

                Row {
                    width: parent.width
                    height: 31
                    spacing: 10

                    PopupText {
                        width: Math.floor(parent.width * 0.30)
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.panelText
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 2
                        font.bold: true
                        text: root.memoryMode ? "memory" : "cpu"
                    }

                    PopupText {
                        width: Math.floor(parent.width * 0.17)
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.memoryMode ? root.memoryGreen : root.usageColor(cpu ? cpu.usage : 0)
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 2
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                        text: root.memoryMode ? root.formatPercent(cpu ? cpu.memoryUsage : 0) : root.formatPercent(cpu ? cpu.usage : 0)
                    }

                    PopupText {
                        width: Math.floor(parent.width * 0.25)
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.panelTextSoft
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("load") + " " + root.formatLoad(cpu ? cpu.loadAverage1 : 0) + " " + root.formatLoad(cpu ? cpu.loadAverage5 : 0) + " " + root.formatLoad(cpu ? cpu.loadAverage15 : 0)
                    }

                    PopupText {
                        width: parent.width - x
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.panelTextSoft
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        horizontalAlignment: Text.AlignRight
                        text: (cpu ? cpu.runningProcesses : 0) + " running / " + (cpu ? cpu.processCount : 0) + " proc"
                    }
                }

                Row {
                    width: parent.width
                    height: 128
                    spacing: 8

                    Rectangle {
                        width: Math.floor((parent.width - parent.spacing) * 0.58)
                        height: parent.height
                        radius: 3
                        color: root.panelGraphBackground
                        border.color: Qt.rgba(1, 1, 1, 0.07)
                        border.width: 1

                        Repeater {
                            model: 4
                            Rectangle {
                                x: Math.round((parent.width / 4) * index)
                                y: 0
                                width: 1
                                height: parent.height
                                color: root.panelGridLine
                            }
                        }

                        Repeater {
                            model: 4
                            Rectangle {
                                x: 0
                                y: Math.round((parent.height / 4) * index)
                                width: parent.width
                                height: 1
                                color: root.panelGridLine
                            }
                        }

                        PopupText {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 8
                            color: root.panelText
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize
                            font.bold: true
                            text: root.memoryMode ? "RAM usage" : "CPU usage"
                        }

                        PopupText {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            color: root.panelTextSoft
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize
                            font.bold: true
                            text: root.memoryMode ? root.formatPercent(cpu ? cpu.memoryUsage : 0) : root.formatPercent(cpu ? cpu.usage : 0)
                        }

                        Sparkline {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 28
                            anchors.bottomMargin: 10
                            values: root.memoryMode ? (cpu ? cpu.swapUsageHistory : []) : (cpu ? cpu.loadAverageHistory : [])
                            maxValue: root.memoryMode ? 100 : root.loadScale
                            lineWidth: 1.2
                            lineColor: root.memoryMode ? root.swapPink : root.loadAmber
                            fillColor: root.memoryMode ? Qt.rgba(root.swapPink.r, root.swapPink.g, root.swapPink.b, 0.20) : Qt.rgba(root.loadAmber.r, root.loadAmber.g, root.loadAmber.b, 0.16)
                        }

                        Sparkline {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 28
                            anchors.bottomMargin: 10
                            values: root.memoryMode ? (cpu ? cpu.memoryUsageHistory : []) : (cpu ? cpu.usageHistory : [])
                            maxValue: 100
                            lineWidth: 1.8
                            lineColor: root.memoryMode ? root.memoryGreen : root.usageColor(cpu ? cpu.usage : 0)
                            fillColor: root.memoryMode
                                ? Qt.rgba(root.memoryGreen.r, root.memoryGreen.g, root.memoryGreen.b, 0.26)
                                : Qt.rgba(root.usageColor(cpu ? cpu.usage : 0).r, root.usageColor(cpu ? cpu.usage : 0).g, root.usageColor(cpu ? cpu.usage : 0).b, 0.24)
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            spacing: 12

                            PopupText {
                                color: root.memoryMode ? root.memoryGreen : root.cpuBlue
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                font.bold: true
                                text: root.memoryMode ? "mem" : "usage"
                            }

                            PopupText {
                                color: root.memoryMode ? root.swapPink : root.loadAmber
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                font.bold: true
                                text: root.memoryMode ? "swap" : "load"
                            }
                        }
                    }

                    Column {
                        width: parent.width - x
                        height: parent.height
                        spacing: 8

                        Rectangle {
                            width: parent.width
                            height: 60
                            radius: 3
                            color: root.panelBackgroundAlt
                            border.color: Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 5

                                Row {
                                    width: parent.width
                                    spacing: 8

                                    PopupText {
                                        width: parent.width * 0.45
                                        color: root.panelTextSoft
                                        font.family: theme.fontFamily
                                        font.pointSize: theme.fontSize - 1
                                        font.bold: true
                                        text: root.memoryMode ? "cpu" : "memory"
                                    }

                                    PopupText {
                                        width: parent.width - x
                                        color: root.panelTextSoft
                                        font.family: theme.fontFamily
                                        font.pointSize: theme.fontSize
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        text: root.memoryMode ? root.formatPercent(cpu ? cpu.usage : 0) : root.formatPercent(cpu ? cpu.memoryUsage : 0)
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 10
                                    radius: 2
                                    color: Qt.rgba(0, 0, 0, 0.22)

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: parent.width * (root.memoryMode ? root.clamp01((cpu ? cpu.usage : 0) / 100.0) : root.clamp01((cpu ? cpu.memoryUsage : 0) / 100.0))
                                        radius: 2
                                        color: root.memoryMode ? root.usageColor(cpu ? cpu.usage : 0) : root.memoryGreen
                                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                    }
                                }

                                PopupText {
                                    width: parent.width
                                    color: root.panelTextSoft
                                    font.family: theme.fontFamily
                                    font.pointSize: theme.fontSize - 1
                                    elide: Text.ElideRight
                                    text: root.memoryMode ? ((cpu ? cpu.coreCount : 0) + " logical cores") : ("swap " + root.formatPercent(cpu ? cpu.swapUsage : 0))
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 60
                            radius: 3
                            color: root.panelBackgroundAlt
                            border.color: Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1

                            Sparkline {
                                anchors.fill: parent
                                anchors.margins: 8
                                values: root.memoryMode ? (cpu ? cpu.usageHistory : []) : (cpu ? cpu.memoryUsageHistory : [])
                                maxValue: 100
                                lineWidth: 1.5
                                lineColor: root.memoryMode ? root.cpuBlue : root.memoryGreen
                                fillColor: root.memoryMode ? Qt.rgba(root.cpuBlue.r, root.cpuBlue.g, root.cpuBlue.b, 0.20) : Qt.rgba(root.memoryGreen.r, root.memoryGreen.g, root.memoryGreen.b, 0.20)
                            }

                            PopupText {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 8
                                color: root.panelTextSoft
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                font.bold: true
                                text: root.memoryMode ? "cpu history" : "memory history"
                            }
                        }
                    }
                }
            }
        }

        // RAM breakdown (memory popup only): the honest 3-way split — in use by
        // processes, reclaimable cache/buffers, and truly free — so cache/buffers
        // aren't miscounted as "used". used = total - MemAvailable; free = MemFree;
        // the middle segment is the reclaimable remainder.
        Rectangle {
            id: ramBar
            visible: root.memoryMode
            width: contentColumn.width
            height: visible ? 58 : 0
            radius: 4
            color: root.panelBackground
            border.color: root.panelBorder
            border.width: 1

            readonly property real memTotal: cpu ? cpu.memoryTotalBytes : 0
            readonly property real memUsed: cpu ? cpu.memoryUsedBytes : 0
            readonly property real memFree: cpu ? cpu.memoryFreeBytes : 0
            readonly property real memCache: Math.max(0, memTotal - memUsed - memFree)
            function frac(v) { return ramBar.memTotal > 0 ? v / ramBar.memTotal : 0 }

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Rectangle {
                    width: parent.width
                    height: 12
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.22)
                    clip: true

                    Rectangle {
                        id: ramUsedSeg
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * ramBar.frac(ramBar.memUsed)
                        color: root.memoryGreen
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }
                    Rectangle {
                        anchors.left: ramUsedSeg.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * ramBar.frac(ramBar.memCache)
                        color: root.memoryCache
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 16

                    Row {
                        spacing: 5
                        Rectangle { width: 8; height: 8; radius: 2; color: root.memoryGreen; anchors.verticalCenter: parent.verticalCenter }
                        PopupText {
                            color: root.panelTextSoft; font.family: theme.fontFamily; font.pointSize: theme.fontSize - 1
                            text: qsTr("used") + " " + root.formatMemoryKb(ramBar.memUsed / 1024)
                        }
                    }
                    Row {
                        spacing: 5
                        Rectangle { width: 8; height: 8; radius: 2; color: root.memoryCache; anchors.verticalCenter: parent.verticalCenter }
                        PopupText {
                            color: root.panelTextSoft; font.family: theme.fontFamily; font.pointSize: theme.fontSize - 1
                            text: qsTr("cache") + " " + root.formatMemoryKb(ramBar.memCache / 1024)
                        }
                    }
                    Row {
                        spacing: 5
                        Rectangle { width: 8; height: 8; radius: 2; color: Qt.rgba(1, 1, 1, 0.25); anchors.verticalCenter: parent.verticalCenter }
                        PopupText {
                            color: root.panelTextSoft; font.family: theme.fontFamily; font.pointSize: theme.fontSize - 1
                            text: qsTr("free") + " " + root.formatMemoryKb(ramBar.memFree / 1024)
                        }
                    }
                }
            }
        }

        Rectangle {
            width: contentColumn.width
            height: 96
            radius: 4
            color: root.panelBackground
            border.color: root.panelBorder
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Row {
                    width: parent.width

                    PopupText {
                        width: parent.width * 0.55
                        color: root.panelText
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        font.bold: true
                        text: root.memoryMode ? "top memory processes" : "top cpu processes"
                    }

                    PopupText {
                        width: parent.width - x
                        color: root.panelTextSoft
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        horizontalAlignment: Text.AlignRight
                        text: (cpu ? cpu.processCount : 0) + " total"
                    }
                }

                Repeater {
                    id: processRepeater
                    property var rows: root.processList()
                    model: Math.min(3, rows.length)

                    delegate: Rectangle {
                        width: parent.width
                        height: 17
                        radius: 2
                        color: index % 2 === 0 ? Qt.rgba(1, 1, 1, 0.035) : "transparent"

                        readonly property var processItem: processRepeater.rows[index]
                        readonly property real processRatio: root.processWeight(processItem, processRepeater.rows)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Math.max(1, parent.width * processRatio)
                            radius: 2
                            color: root.memoryMode ? Qt.rgba(root.memoryGreen.r, root.memoryGreen.g, root.memoryGreen.b, 0.18) : Qt.rgba(root.cpuBlue.r, root.cpuBlue.g, root.cpuBlue.b, 0.18)
                        }

                        PopupText {
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - valueLabel.width - 16
                            color: root.panelText
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize - 1
                            font.bold: true
                            elide: Text.ElideRight
                            text: root.processName(processItem)
                        }

                        PopupText {
                            id: valueLabel
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.memoryMode ? root.memoryGreen : root.cpuBlue
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize - 1
                            font.bold: true
                            text: root.processValue(processItem)
                        }
                    }
                }
            }
        }

        Grid {
            id: coreGrid
            width: contentColumn.width
            columns: root.coreColumns
            rowSpacing: root.coreSpacing
            columnSpacing: root.coreSpacing

            Repeater {
                model: cpu ? cpu.coreCount : 0

                delegate: Rectangle {
                    id: tile
                    width: Math.floor((coreGrid.width - root.coreSpacing * (root.coreColumns - 1)) / root.coreColumns)
                    height: root.coreTileHeight
                    radius: 4
                    color: index % 2 === 0 ? root.panelBackgroundAlt : root.panelBackground
                    border.color: Qt.rgba(1, 1, 1, 0.07)
                    border.width: 1

                    property int usageValue: cpu ? cpu.coreUsage(index) : 0
                    property string coreName: cpu ? cpu.coreName(index) : String(index)
                    property var coreHistory: cpu ? cpu.coreHistory(index) : []
                    property color chartColor: root.usageColor(usageValue)

                    function syncCoreData() {
                        if (!cpu) {
                            return
                        }
                        usageValue = cpu.coreUsage(index)
                        coreName = cpu.coreName(index)
                        coreHistory = cpu.coreHistory(index)
                        chartColor = root.usageColor(usageValue)
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 5

                        Row {
                            width: parent.width

                            PopupText {
                                width: parent.width - usageText.width
                                color: root.panelText
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                font.bold: true
                                text: tile.coreName
                            }

                            PopupText {
                                id: usageText
                                color: tile.chartColor
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                font.bold: true
                                text: root.formatPercent(tile.usageValue)
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 7
                            radius: 2
                            color: Qt.rgba(0, 0, 0, 0.22)

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: parent.width * root.clamp01(tile.usageValue / 100.0)
                                radius: 2
                                color: tile.chartColor
                                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 38
                            radius: 2
                            color: root.panelGraphBackground
                            border.color: Qt.rgba(1, 1, 1, 0.045)
                            border.width: 1

                            Repeater {
                                model: 3
                                Rectangle {
                                    x: Math.round((parent.width / 3) * index)
                                    width: 1
                                    height: parent.height
                                    color: root.panelGridLine
                                }
                            }

                            Sparkline {
                                anchors.fill: parent
                                anchors.margins: 3
                                values: tile.coreHistory
                                maxValue: 100
                                lineWidth: 1.35
                                lineColor: tile.chartColor
                                fillColor: Qt.rgba(tile.chartColor.r, tile.chartColor.g, tile.chartColor.b, 0.25)
                            }
                        }
                    }

                    Connections {
                        target: root
                        function onCpuChanged() { tile.syncCoreData() }
                    }

                    Connections {
                        target: cpu
                        function onCoresChanged() { tile.syncCoreData() }
                    }

                    Component.onCompleted: tile.syncCoreData()
                }
            }
        }
    }
}
