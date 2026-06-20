import QtQuick
import "qrc:/qbar" as QBar

// Example RUNTIME custom QML widget — loaded from disk, NOT compiled into qbar.
//
// Enable it in your config (~/.config/qbar/config.json):
//   "modules-right": [ "CustomTool:custom/example" ],
//   "customTools": {
//     "custom/example": { "source": "widgets/ExampleWidget.qml", "label": "hi" }
//   }
// `source` is resolved relative to the config dir, so this file lives at
// ~/.config/qbar/widgets/ExampleWidget.qml. A `source` entry loads QML; an `exec`
// entry runs the script-driven CustomTool instead.
//
// A runtime widget has the SAME powers as a built-in applet: it can `import "qrc:/qbar"`
// (CssRect / CssText / CssEnable / CssKeyframes), `import QBar 1.0` (C++ types), and read
// every context property — cssTheme, theme, and all the models (cpuModel, soundModel, …).
// It styles itself through the engine like any applet: declare `cssId` and the theme's
// `#custom-example { ... }` rules apply (incl. transition/@keyframes), pushed via loadCss.
QBar.CssRect {
    id: root

    // The bar sets this to the customTools id (e.g. "custom/example") right after load.
    property string toolId: ""
    readonly property var widgetConfig: (typeof customTools !== "undefined" && customTools && customTools[toolId])
        ? customTools[toolId] : ({})

    // waybar id "custom/example" → CSS selector "#custom-example".
    cssId: toolId.replace("/", "-")
    height: theme.height
    width: Math.max(1, label.implicitWidth + 16)

    // Applet sizing contract: let the bar lay this out by its content width.
    signal preferredWidthUpdated(int width)
    onWidthChanged: preferredWidthUpdated(width)
    Component.onCompleted: preferredWidthUpdated(width)

    QBar.CssText {
        id: label
        cssId: root.cssId
        anchors.centerIn: parent
        text: root.widgetConfig.label !== undefined ? root.widgetConfig.label : "widget"
    }
}
