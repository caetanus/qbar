import QtQuick
import QBar 1.0
import "qrc:/qbar" as Chrome

Chrome.CssRect {
    id: root

    property string toolId: ""
    property var toolConfig: (customTools && toolId !== "" && customTools[toolId]) ? customTools[toolId] : ({})
    property bool initialized: false
    property alias toolModel: toolModel

    // waybar module names use "custom/foo"; CSS selectors use "#custom-foo"
    cssId: toolId.replace("/", "-")
    readonly property var cssStyle: root.style

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
        // Waybar default is plain text; JSON only when return-type is "json".
        toolModel.waybarFormat = cfg["return-type"] === "json"
        toolModel.format = cfg.format !== undefined ? cfg.format : "{}"
        toolModel.formatIcons = cfg["format-icons"] || {}
        if (!root.initialized) {
            root.initialized = true
        }
        toolModel.refresh()
    }

    onToolIdChanged: applyConfig()
    onToolConfigChanged: applyConfig()
    onImplicitWidthChanged: preferredWidthUpdated(Math.max(1, implicitWidth))
    Component.onCompleted: applyConfig()

    // Background painted by the CssRect base.

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        Text {
            id: outputText
            text: toolModel.displayText

            readonly property var dropShadow: cssTheme && cssTheme.loaded
                ? cssTheme.parseBoxShadow(root.cssStyle["text-shadow"] || "") : ({})
            layer.enabled: dropShadow.color !== undefined
            layer.effect: Chrome.CssDropShadow { shadow: outputText.dropShadow }

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
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: Qt.PointingHandCursor

        function action(key) {
            var cfg = root.toolConfig || {}
            return cfg[key] || ""
        }

        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                toolModel.runAction(action("on-click-right"))
            } else if (mouse.button === Qt.MiddleButton) {
                toolModel.runAction(action("on-click-middle"))
            } else if (action("on-click").length > 0) {
                toolModel.runAction(action("on-click"))
            } else {
                toolModel.refresh()
            }
        }

        onWheel: function(wheel) {
            if (wheel.angleDelta.y > 0) {
                toolModel.runAction(action("on-scroll-up"))
            } else if (wheel.angleDelta.y < 0) {
                toolModel.runAction(action("on-scroll-down"))
            }
        }
    }
}
