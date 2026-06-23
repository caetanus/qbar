import QtQuick
import QBar 1.0

// Network popup: a condensed rolling 5-minute throughput graph (download filled area +
// upload line) over NetworkModel's downloadHistory5m / uploadHistory5m, plus current rates.
Item {
    id: root

    property var net: null
    property var procs: null
    readonly property var topTalkers: procs ? procs.topTalkers : []
    readonly property var dl: net ? net.downloadHistory5m : []
    readonly property var ul: net ? net.uploadHistory5m : []
    // Adaptive window: the X axis grows 1→2→3→5 min as data accumulates.
    readonly property int windowSec: net ? net.historyWindowSeconds : 300
    readonly property string windowLabel: (windowSec / 60) + " min"

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color dlColor: "#5aa9e6"
    readonly property color ulColor: "#e8954a"

    // Shared vertical scale = peak across both series in the window (≥1 to avoid /0).
    readonly property real maxv: {
        var m = 1
        for (var i = 0; i < dl.length; ++i) m = Math.max(m, dl[i])
        for (var j = 0; j < ul.length; ++j) m = Math.max(m, ul[j])
        return m
    }

    implicitWidth: 380
    width: implicitWidth
    implicitHeight: col.implicitHeight + 24
    height: implicitHeight

    function fmt(bps) {
        var units = ["B/s", "KB/s", "MB/s", "GB/s"]
        var v = bps
        var i = 0
        while (v >= 1024 && i < units.length - 1) { v /= 1024; ++i }
        return (v >= 100 ? v.toFixed(0) : v.toFixed(1)) + " " + units[i]
    }

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10

        Text {
            text: "Network — last " + root.windowLabel
            color: root.fg
            font.bold: true
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize + 1
        }

        // Graph panel.
        Rectangle {
            width: parent.width
            height: 132
            radius: 6
            color: Qt.rgba(0, 0, 0, 0.22)
            border.width: 1
            border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.10)
            clip: true

            Sparkline {
                anchors.fill: parent
                anchors.margins: 5
                values: root.dl
                maxValue: root.maxv
                lineColor: root.dlColor
                fillColor: Qt.rgba(root.dlColor.r, root.dlColor.g, root.dlColor.b, 0.22)
                lineWidth: 1.6
            }
            Sparkline {
                anchors.fill: parent
                anchors.margins: 5
                values: root.ul
                maxValue: root.maxv
                lineColor: root.ulColor
                fillColor: "transparent"
                lineWidth: 1.6
            }

            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 5
                text: "peak " + root.fmt(root.maxv)
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 2
            }
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 5
                text: "−" + root.windowLabel
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 2
            }
            Text {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 5
                text: "now"
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 2
            }
        }

        // Current rates legend.
        Row {
            spacing: 22

            Row {
                spacing: 7
                Rectangle { width: 9; height: 9; radius: 2; color: root.dlColor; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↓ " + root.fmt(root.net ? root.net.downloadRateBytesPerSecond : 0)
                    color: root.fg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
            Row {
                spacing: 7
                Rectangle { width: 9; height: 9; radius: 2; color: root.ulColor; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↑ " + root.fmt(root.net ? root.net.uploadRateBytesPerSecond : 0)
                    color: root.fg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }

        // Top processes by network (TCP, last interval). Empty until two samples land.
        Column {
            width: parent.width
            spacing: 3
            visible: root.topTalkers.length > 0

            Text {
                text: "Top processes"
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 1
            }
            Repeater {
                model: root.topTalkers
                delegate: Item {
                    required property var modelData
                    width: parent.width
                    height: 18
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - rates.width - 8
                        text: modelData.name + " (" + modelData.pid + ")"
                        color: root.fg
                        elide: Text.ElideRight
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize - 1
                    }
                    Text {
                        id: rates
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "↓" + root.fmt(modelData.download) + "  ↑" + root.fmt(modelData.upload)
                        color: root.fgSoft
                        font.family: "monospace"
                        font.pointSize: theme.fontSize - 1
                    }
                }
            }
        }
    }
}
