import QtQuick

Item {
    id: root
    width: 560
    implicitWidth: 560
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight

    property var cpu: null
    property string popupMode: (typeof popupData !== "undefined" && popupData && popupData.popupMode !== undefined) ? String(popupData.popupMode) : "cpu"
    property int coreColumns: 2
    property int coreTileHeight: 96
    property int coreSpacing: 8

    function formatPercent(value) {
        if (value === undefined || value === null) {
            return "--"
        }
        return Number(value) + "%"
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

    function formatTopProcesses(list, mode) {
        if (!list || list.length === 0) {
            return "--"
        }

        var parts = []
        for (var i = 0; i < list.length; ++i) {
            var item = list[i]
            if (!item) {
                continue
            }
            var name = item.name !== undefined ? item.name : "pid " + item.pid
            if (mode === "memory") {
                parts.push(name + " " + root.formatMemoryKb(item.memoryKb))
            } else {
                var usage = item.usage !== undefined ? Number(item.usage).toFixed(1) : "0.0"
                parts.push(name + " " + usage + "%")
            }
        }
        return parts.join("   ")
    }

    function drawSeries(ctx, points, width, height, fillColor, strokeColor, scale, fillAlpha) {
        if (!points || points.length === 0) {
            return
        }

        var maxValue = scale && scale > 0 ? scale : 100
        ctx.beginPath()
        ctx.moveTo(0, height)
        for (var i = 0; i < points.length; ++i) {
            var value = Number(points[i])
            if (isNaN(value)) {
                value = 0
            }
            var normalized = Math.max(0, Math.min(1, value / maxValue))
            var x = points.length === 1 ? width - 1 : (i * (width - 1)) / (points.length - 1)
            var y = height - Math.max(1, normalized * (height - 2)) - 1
            ctx.lineTo(x, y)
        }
        ctx.lineTo(width - 1, height)
        ctx.closePath()
        ctx.save()
        ctx.globalAlpha = fillAlpha !== undefined ? fillAlpha : 1.0
        ctx.fillStyle = fillColor
        ctx.fill()
        ctx.restore()

        ctx.beginPath()
        for (var j = 0; j < points.length; ++j) {
            var sample = Number(points[j])
            if (isNaN(sample)) {
                sample = 0
            }
            var px = points.length === 1 ? width - 1 : (j * (width - 1)) / (points.length - 1)
            var py = height - Math.max(1, Math.min(1, sample / maxValue) * (height - 2)) - 1
            if (j === 0) {
                ctx.moveTo(px, py)
            } else {
                ctx.lineTo(px, py)
            }
        }
        ctx.strokeStyle = strokeColor
        ctx.lineWidth = 1.4
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        ctx.stroke()
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
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
            height: 82
            radius: 3
            color: "#24303a"

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 5

                Row {
                    spacing: 10

                    Text {
                        text: root.formatPercent(cpu ? cpu.usage : 0) + " cpu usage"
                        color: "#ffffff"
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 1
                        font.bold: true
                    }

                    Text {
                        text: "load avg " + root.formatLoad(cpu ? cpu.loadAverage1 : 0) + " " + root.formatLoad(cpu ? cpu.loadAverage5 : 0) + " " + root.formatLoad(cpu ? cpu.loadAverage15 : 0)
                        color: "#d1d5db"
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                    }
                }

                Row {
                    width: parent.width
                    spacing: 10

                    Text {
                        text: (cpu ? cpu.processCount : 0) + " processes"
                        color: "#ffffff"
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                    }

                    Text {
                        width: parent.width - implicitWidth
                        text: (root.popupMode === "memory" ? "top memory: " : "top: ") + root.formatTopProcesses(cpu ? (root.popupMode === "memory" ? cpu.topMemoryProcesses : cpu.topProcesses) : [], root.popupMode)
                        color: "#d1d5db"
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    width: Math.max(1, Math.floor(parent.width * 0.7))
                    height: 29
                    radius: 2
                    color: "transparent"
                    anchors.horizontalCenter: parent.horizontalCenter

                    Canvas {
                        id: memoryGraph
                        anchors.fill: parent

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            root.drawSeries(ctx, cpu ? cpu.swapUsageHistory : [], width, height, "#ec4899", "#ec4899", 100, 0.60)
                            root.drawSeries(ctx, cpu ? cpu.memoryUsageHistory : [], width, height, "#84cc16", "#84cc16", 100, 0.60)
                        }
                    }

                    Connections {
                        target: root
                        function onCpuChanged() { memoryGraph.requestPaint() }
                    }

                    Connections {
                        target: cpu
                        function onMemoryStatsChanged() { memoryGraph.requestPaint() }
                    }

                    Component.onCompleted: memoryGraph.requestPaint()
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
                    radius: 3
                    color: index % 2 === 0 ? "#2a363f" : "#24303a"

                    property int usageValue: cpu ? cpu.coreUsage(index) : 0
                    property string coreName: cpu ? cpu.coreName(index) : String(index)
                    property var coreHistory: cpu ? cpu.coreHistory(index) : []
                    property bool hot: usageValue > 50
                    property color chartStroke: hot ? "#f43f5e" : "#63b3ed"
                    property color chartFill: hot ? "#f43f5e" : "#63b3ed"

                    function syncCoreData() {
                        if (!cpu) {
                            return
                        }
                        usageValue = cpu.coreUsage(index)
                        coreName = cpu.coreName(index)
                        coreHistory = cpu.coreHistory(index)
                        hot = usageValue > 50
                        chartStroke = hot ? "#f43f5e" : "#63b3ed"
                        chartFill = hot ? "#f43f5e" : "#63b3ed"
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                width: 34
                                color: "#ffffff"
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize
                                text: tile.coreName
                            }

                            Text {
                                width: 44
                                color: "#d1d5db"
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize
                                font.bold: true
                                text: root.formatPercent(usageValue)
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 34
                            radius: 2
                            color: "transparent"

                            Canvas {
                                id: coreChart
                                anchors.fill: parent

                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    root.drawSeries(ctx, tile.coreHistory, width, height, tile.chartFill, tile.chartStroke, 100, 0.60)
                                }
                            }
                        }
                    }

                    Connections {
                        target: root
                        function onCpuChanged() { coreChart.requestPaint() }
                    }

                    Connections {
                        target: cpu
                        function onCoresChanged() {
                            tile.syncCoreData()
                            coreChart.requestPaint()
                        }
                    }

                    Component.onCompleted: {
                        tile.syncCoreData()
                        coreChart.requestPaint()
                    }
                }
            }
        }
    }
}
