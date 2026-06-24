import QtQuick
import "qrc:/qbar" as QBar

// In-bar proxy for the macOS-style Dock — the module users add as an alternative to
// the Taskbar ("Dock" in modules-*). It reserves the dock's footprint in the bar
// layout and stays transparent, so a theme can leave the bar centre transparent and
// still "have" the dock there. It then reports its on-screen rectangle to the
// DockWindow controller (`dockController`), which floats the ACTUAL dock — its own
// window — over this slot. The dock is a separate window so the cursor magnification
// can overflow the bar's bounds without being clipped.
//
// Width is reserved for one icon slot per running window; the constants mirror
// DockSurface.qml so the reserved space matches what the dock window paints.
QBar.CssRect {
    id: root
    cssId: "dock"
    height: theme.height
    // Transparent by default (CssRect.defaultColor) — a theme may still style #dock.

    readonly property int iconBase: Math.round(theme.height * 0.78)
    readonly property int spacing: 6
    readonly property int count: counter.count

    property int preferredWidth: count > 0
        ? count * iconBase + (count - 1) * spacing + 16
        : 0
    width: Math.max(1, preferredWidth)

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: { preferredWidthUpdated(preferredWidth); Qt.callLater(report) }
    Component.onCompleted: { preferredWidthUpdated(preferredWidth); Qt.callLater(report) }

    // Counts the windows without drawing anything (Repeater.count == model rows).
    Repeater { id: counter; model: windowModel ? windowModel : 0; delegate: Item {} }

    function report() {
        if (!dockController)
            return
        if (root.preferredWidth <= 0 || root.width <= 1) {
            dockController.hideDock()
            return
        }
        var g = root.mapToGlobal(0, 0)
        dockController.setSlotGeometry(Math.round(g.x), Math.round(g.y),
                                       Math.round(root.width), Math.round(root.height))
    }

    onXChanged: Qt.callLater(report)
    onWidthChanged: Qt.callLater(report)

    // The dock window must track the bar moving/resizing (multi-monitor, layout shifts).
    Connections {
        target: barWindow
        ignoreUnknownSignals: true
        function onXChanged() { Qt.callLater(root.report) }
        function onYChanged() { Qt.callLater(root.report) }
        function onWidthChanged() { Qt.callLater(root.report) }
        function onScreenChanged() { Qt.callLater(root.report) }
    }
}
