import QtQuick
import "qrc:/qbar" as QBar

// Popup for the speed-test widget — EXTERNAL (loaded from disk by SpeedTest.qml). Shows the
// download figure (live while testing, peak when done), ping, a gauge, and a Start button.
// `engine` (the widget) arrives via the payload; we bind its state and call startTest().
Item {
    id: root
    implicitWidth: 300
    implicitHeight: 295
    width: implicitWidth
    height: implicitHeight

    property var engine: null

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground
    readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.6)
    readonly property color fgFaint: Qt.rgba(fg.r, fg.g, fg.b, 0.3)
    readonly property color accent: (cssTheme && cssTheme.loaded && theme.accent !== undefined)
        ? cssTheme.parseColor(theme.accent) : fg

    readonly property bool running: engine ? engine.running : false
    readonly property string phase: engine ? engine.phase : "idle"
    // The big readout follows the active phase: live download, then live upload, then the
    // final download figure once done.
    readonly property real shownMbps: !engine ? 0
        : (phase === "download" ? engine.liveDownloadMbps
        : (phase === "upload" ? engine.liveUploadMbps
        : (phase === "done" ? engine.downloadMbps : 0)))
    readonly property string bigCaption: phase === "upload" ? "Upload" : "Download"
    // Upload figure for its column: live while uploading, final otherwise.
    readonly property real uploadShown: !engine ? 0
        : (phase === "upload" ? engine.liveUploadMbps : engine.uploadMbps)
    readonly property real pingMs: engine ? engine.pingMs : 0
    // Gauge scale: round up to a sensible ceiling above the value.
    readonly property real gaugeMax: Math.max(50, Math.pow(10, Math.ceil(Math.log(Math.max(1, shownMbps) + 1) / Math.LN10)))

    function statusText() {
        if (phase === "ping") return "Pinging…"
        if (phase === "download") return "Measuring download…"
        if (phase === "upload") return "Measuring upload…"
        if (phase === "done") return "Done"
        if (phase === "error") return "Failed"
        return "Ready"
    }

    function fmtMbps(v) {
        if (v <= 0) return "—"
        return v >= 100 ? v.toFixed(0) : v.toFixed(1)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Item {
            width: parent.width
            height: title.implicitHeight
            Text {
                id: title
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Speed test"
                color: root.fg
                font.bold: true
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 1
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.statusText()
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 1
            }
        }

        // Big readout — follows the active phase (download → upload → final download).
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 2
            // Caption ABOVE the number.
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.bigCaption
                color: root.fgSoft
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 2
            }
            // Number + unit, baseline-aligned. An Item with anchored children (NOT a Row —
            // a Row can't position a child that carries its own anchors).
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: bigNum.width + unit.width + 6
                height: bigNum.height
                Text {
                    id: bigNum
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: root.fmtMbps(root.shownMbps)
                    color: root.fg
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize + 18
                    font.bold: true
                }
                Text {
                    id: unit
                    anchors.left: bigNum.right
                    anchors.leftMargin: 6
                    anchors.baseline: bigNum.baseline
                    text: "Mbps"
                    color: root.fgSoft
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
        }

        // Gauge.
        Rectangle {
            width: parent.width
            height: 8
            radius: 4
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.12)
            Rectangle {
                height: parent.height
                radius: parent.radius
                width: parent.width * Math.max(0, Math.min(1, root.shownMbps / root.gaugeMax))
                color: root.accent
                Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
            }
        }

        // Ping + upload.
        Row {
            width: parent.width
            spacing: 24
            Column {
                spacing: 2
                Text { text: "Ping"; color: root.fgSoft; font.family: theme.fontFamily; font.pointSize: theme.fontSize - 2 }
                Text {
                    text: root.pingMs > 0 ? root.pingMs + " ms" : "—"
                    color: root.fg; font.family: theme.fontFamily; font.pointSize: theme.fontSize
                }
            }
            Column {
                spacing: 2
                Text { text: "Upload"; color: root.fgSoft; font.family: theme.fontFamily; font.pointSize: theme.fontSize - 2 }
                Text {
                    text: root.uploadShown > 0 ? root.fmtMbps(root.uploadShown) + " Mbps" : "—"
                    color: root.uploadShown > 0 ? root.fg : root.fgFaint
                    font.family: theme.fontFamily; font.pointSize: theme.fontSize
                }
            }
        }

        // Start / Retest button.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: btnLabel.implicitWidth + 28
            height: btnLabel.implicitHeight + 12
            radius: height / 2
            color: root.running ? "transparent" : Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)
            border.width: 1
            border.color: root.running ? root.fgFaint : root.accent
            opacity: root.running ? 0.5 : 1.0

            Text {
                id: btnLabel
                anchors.centerIn: parent
                text: root.running ? "Running…" : (root.phase === "done" ? "Retest" : "Start")
                color: root.running ? root.fgSoft : root.accent
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                enabled: !root.running
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: if (root.engine) root.engine.startTest()
            }
        }
    }
}
