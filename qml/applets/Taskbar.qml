import QtQuick
import QtQml
import "qrc:/qbar" as QBar

QBar.CssRect {
    id: root
    cssId: "taskbar"
    height: theme.height
    width: Math.max(1, preferredWidth)
    clip: true

    readonly property var cssStyle: root.style

    // Hidden (width 0) when there are no matching windows — the qbar pattern.
    property int preferredWidth: row.visibleChildrenWidth > 0 ? Math.ceil(row.visibleChildrenWidth + 8) : 0
    property int maxTitleWidth: cssStyle["max-width"] ? parseInt(cssStyle["max-width"]) : 160
    property int itemSpacing: 4

    readonly property string scope: taskbarConfig && taskbarConfig.scope ? taskbarConfig.scope : "workspace"
    readonly property bool middleClickClose: !taskbarConfig || taskbarConfig.middleClickClose !== false
    readonly property bool rightClickMenu: !taskbarConfig || taskbarConfig.rightClickMenu !== false
    readonly property string barMonitor: barWindow && barWindow.screen ? barWindow.screen.name : ""

    // The active workspace is the one holding the focused window; captured from
    // the model so "workspace" scope works without scanning workspaceModel.
    property string activeWorkspace: ""

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // theme.* colours are HexArgb STRINGS, so `theme.foreground.r` is undefined and
    // Qt.rgba(undefined,...) renders black. Parse first, then tint.
    function alphaColor(colorStr, a) {
        var c = cssTheme.parseColor(colorStr)
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // Per-button CSS: `#taskbar button[.focused/.urgent]` (mirrors #workspaces button).
    // Lets a theme style the running-app buttons — e.g. the bliss-xp raised/pressed
    // Luna buttons — instead of the flat fallback tint below.
    function buttonStyle(classes) {
        return cssTheme && cssTheme.loaded ? cssTheme.resolveWith("taskbar", "button", classes) : ({})
    }

    // Wheel cycles focus through the currently-visible windows in row order.
    // direction +1 = next, -1 = previous.
    function cycle(direction) {
        if (!wm) {
            return
        }
        var items = []
        for (var i = 0; i < row.children.length; i++) {
            var child = row.children[i]
            if (child.visible && child.windowId !== undefined) {
                items.push(child)
            }
        }
        if (items.length === 0) {
            return
        }
        var current = -1
        for (var j = 0; j < items.length; j++) {
            if (items[j].focused) {
                current = j
                break
            }
        }
        var next = current < 0 ? 0 : (current + direction + items.length) % items.length
        wm.activateWindow(items[next].windowId)
    }

    function windowIncluded(workspaceName, monitor) {
        if (root.scope === "all") {
            return true
        }
        if (root.scope === "monitor") {
            return root.barMonitor.length === 0 || monitor === root.barMonitor
        }
        return workspaceName === root.activeWorkspace
    }

    Instantiator {
        model: windowModel ? windowModel : 0
        delegate: QtObject {
            required property bool focused
            required property string workspaceName
            function sync() { if (focused) root.activeWorkspace = workspaceName }
            onFocusedChanged: sync()
            onWorkspaceNameChanged: sync()
            Component.onCompleted: sync()
        }
    }

    // Background painted by the CssRect base.

    // Catches wheel scrolls over the gaps between items (the item MouseAreas
    // handle wheel over the items themselves). Scroll up = previous, down = next.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: function(wheel) {
            root.cycle(wheel.angleDelta.y > 0 ? -1 : 1)
        }
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 4
        spacing: root.itemSpacing

        // Width of only the visible delegates (invisible ones are skipped by the
        // positioner, so they add no spacing either).
        readonly property int visibleChildrenWidth: {
            var total = 0
            var count = 0
            for (var i = 0; i < children.length; i++) {
                var child = children[i]
                if (child.visible && child.width > 0) {
                    total += child.width
                    count += 1
                }
            }
            return count > 0 ? total + (count - 1) * spacing : 0
        }

        Repeater {
            model: windowModel ? windowModel : 0

            delegate: Item {
                id: entry
                required property int index
                required property var windowId
                required property string title
                required property string appId
                required property string workspaceName
                required property string monitor
                required property bool focused
                required property bool urgent

                visible: root.windowIncluded(workspaceName, monitor)
                height: root.height
                width: visible ? Math.ceil(icon.width + 6 + label.width + 12) : 0

                // `#taskbar button` + state class for this window's status.
                readonly property var btnClasses: entry.urgent ? ["urgent"]
                    : entry.focused ? ["focused"] : []
                readonly property var btnStyle: root.buttonStyle(entry.btnClasses)
                // Flat tint when the theme doesn't style #taskbar button (unchanged behaviour).
                readonly property color btnFallback: entry.urgent ? root.alphaColor(theme.accent, 0.35)
                    : entry.focused ? root.alphaColor(theme.foreground, 0.16) : "transparent"

                QBar.CssFill {
                    anchors.fill: parent
                    style: entry.btnStyle
                    radius: entry.btnStyle["border-radius"]
                        ? cssTheme.parseLength(entry.btnStyle["border-radius"], 3) : 3
                    defaultColor: entry.btnFallback
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    spacing: 6

                    Image {
                        id: icon
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.round(root.height * 0.6)
                        height: width
                        sourceSize.width: width
                        sourceSize.height: width
                        fillMode: Image.PreserveAspectFit
                        source: entry.appId.length > 0 ? "image://themeicon/" + entry.appId : ""
                        visible: status === Image.Ready
                    }

                    Text {
                        id: label
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, root.maxTitleWidth)
                        elide: Text.ElideRight
                        text: entry.title.length > 0 ? entry.title : entry.appId
                        color: entry.btnStyle["color"] ? cssTheme.parseColor(entry.btnStyle["color"])
                            : cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"])
                            : (entry.focused ? theme.foreground : root.alphaColor(theme.foreground, 0.75))
                        font.family: entry.btnStyle["font-family"] || cssStyle["font-family"] || theme.fontFamily
                        font.pointSize: theme.fontSize
                        font.bold: entry.focused
                    }
                }

                QBar.Tooltip {
                    anchorItem: entry
                    hovered: entryMouse.containsMouse
                    text: entry.workspaceName.length > 0
                        ? (entry.title.length > 0 ? entry.title : entry.appId) + "  ·  " + entry.workspaceName
                        : (entry.title.length > 0 ? entry.title : entry.appId)
                }

                MouseArea {
                    id: entryMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                    onWheel: function(wheel) {
                        root.cycle(wheel.angleDelta.y > 0 ? -1 : 1)
                    }
                    onClicked: function(mouse) {
                        if (!wm) {
                            return
                        }
                        if (mouse.button === Qt.LeftButton) {
                            wm.activateWindow(entry.windowId)
                        } else if (mouse.button === Qt.MiddleButton) {
                            if (root.middleClickClose) {
                                wm.closeWindow(entry.windowId)
                            }
                        } else if (mouse.button === Qt.RightButton) {
                            if (root.rightClickMenu) {
                                root.menuTargetId = entry.windowId
                                taskMenu.model = [
                                    { text: qsTr("Focus"), action: "focus" },
                                    { text: qsTr("Close"), action: "close" },
                                ]
                                taskMenu.anchorItem = entry
                                taskMenu.toggle()
                            }
                        }
                    }
                }
            }
        }
    }

    property var menuTargetId: undefined

    QBar.MenuPopup {
        id: taskMenu
        anchorItem: root
        gap: 4
        onTriggered: function(index, item) {
            if (!wm || root.menuTargetId === undefined) {
                return
            }
            if (item.action === "focus") {
                wm.activateWindow(root.menuTargetId)
            } else if (item.action === "close") {
                wm.closeWindow(root.menuTargetId)
            }
        }
    }
}
