import QtQuick
import QBar 1.0

Item {
    id: root

    property string toolId: ""
    property var toolConfig: (customTools && toolId !== "" && customTools[toolId]) ? customTools[toolId] : ({})
    property bool initialized: false

    // waybar module names use "custom/foo"; CSS selectors use "#custom-foo"
    readonly property string cssId: toolId.replace("/", "-")
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    signal preferredWidthUpdated(int width)

    implicitHeight: theme.height
    implicitWidth: Math.max(1, contentRow.implicitWidth + 10)
    width: implicitWidth
    height: implicitHeight

    CustomToolModel {
        id: toolModel
    }

    function applyConfig() {
        var cfg = root.toolConfig || {}
        toolModel.command = cfg.exec || cfg.command || ""
        toolModel.arguments = cfg.arguments || []
        toolModel.workingDirectory = cfg.workingDirectory || ""
        toolModel.intervalMs = ((cfg.interval !== undefined ? cfg.interval : (cfg.intervalMs !== undefined ? cfg.intervalMs : 10)) * 1000)
        toolModel.waybarFormat = cfg["return-type"] === "json" || cfg.waybarFormat === undefined
        if (!root.initialized) {
            root.initialized = true
        }
        toolModel.refresh()
    }

    onToolIdChanged: applyConfig()
    onToolConfigChanged: applyConfig()
    onImplicitWidthChanged: preferredWidthUpdated(Math.max(1, implicitWidth))
    Component.onCompleted: applyConfig()

    Rectangle {
        anchors.fill: parent
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "transparent"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        Text {
            id: outputText
            text: toolModel.text
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
            textFormat: Text.RichText
            visible: text.length > 0
            elide: Text.ElideRight
            onImplicitWidthChanged: root.preferredWidthUpdated(Math.max(1, implicitWidth + 10))
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: toolModel.refresh()
    }
}
