import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    anchors.fill: parent
    clip: true

    // Merge window#waybar { } (waybar-compat) with #qbar { } (qbar-native).
    // #qbar wins: lets users override waybar defaults without touching the waybar rule.
    readonly property var barStyle: {
        if (!cssTheme || !cssTheme.loaded) return ({})
        var s = cssTheme.resolve("waybar")
        var q = cssTheme.resolveExact("qbar")
        for (var k in q) s[k] = q[k]
        return s
    }
    readonly property color barBg: barStyle["background-color"]
        ? cssTheme.parseColor(barStyle["background-color"])
        : theme.background
    readonly property real barRadius: barStyle["border-radius"] ? parseFloat(barStyle["border-radius"]) : 0
    // border-left/right is the waybar idiom for horizontal bar insets (makes rounded corners visible)
    // margin-left/right is the more semantic alternative; border-* wins if both are set
    readonly property real barMarginLeft: {
        if (barStyle["border-left"]) return parseFloat(barStyle["border-left"])
        if (barStyle["margin-left"]) return parseFloat(barStyle["margin-left"])
        return 0
    }
    readonly property real barMarginRight: {
        if (barStyle["border-right"]) return parseFloat(barStyle["border-right"])
        if (barStyle["margin-right"]) return parseFloat(barStyle["margin-right"])
        return 0
    }

    // CSS padding (shorthand or per-side) — horizontal inset between the rounded
    // background and the applet content. Needed to make border-radius visible,
    // since edge applets otherwise sit flush against the bar's corners.
    function paddingSide(style, side) {
        var explicit = style["padding-" + side]
        if (explicit) return parseFloat(explicit)
        var shorthand = style["padding"]
        if (!shorthand) return 0
        var parts = shorthand.trim().split(/\s+/)
        if (parts.length === 1) return parseFloat(parts[0])
        if (parts.length === 2) return (side === "top" || side === "bottom") ? parseFloat(parts[0]) : parseFloat(parts[1])
        if (parts.length === 3) {
            if (side === "top") return parseFloat(parts[0])
            if (side === "bottom") return parseFloat(parts[2])
            return parseFloat(parts[1])
        }
        if (side === "top") return parseFloat(parts[0])
        if (side === "right") return parseFloat(parts[1])
        if (side === "bottom") return parseFloat(parts[2])
        return parseFloat(parts[3])
    }
    readonly property real barPaddingLeft: root.paddingSide(barStyle, "left")
    readonly property real barPaddingRight: root.paddingSide(barStyle, "right")
    readonly property var leftStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("left") : ({})
    readonly property var centerStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("center") : ({})
    readonly property var rightStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("right") : ({})

    function cssPixels(value, fallback) {
        if (value === undefined || value === null || value === "") {
            return fallback
        }
        var n = parseFloat(value)
        return isNaN(n) ? fallback : n
    }

    function styleRadius(style) {
        return root.cssPixels(style ? style["border-radius"] : undefined, 0)
    }

    function stylePadding(style, side) {
        return root.paddingSide(style || ({}), side)
    }

    function styleMargin(style, side) {
        return root.cssPixels(style ? style["margin-" + side] : undefined, 0)
    }

    function appletUrl(name) {
        if (name.indexOf("CustomTool:") === 0) {
            // A custom entry with a `source` is a RUNTIME QML widget (user-provided,
            // not compiled in); one with `exec` is the script-driven CustomTool.
            var id = name.substring("CustomTool:".length)
            var def = (typeof customTools !== "undefined" && customTools && customTools[id]) ? customTools[id] : null
            if (def && def.source && String(def.source).length > 0)
                return root.resolveWidgetUrl(String(def.source))
            return "qrc:/applets/CustomTool.qml"
        }
        return "qrc:/applets/" + name + ".qml"
    }

    // Resolve a custom-widget `source` to a loadable URL: a qrc:/ or file:/ URL is used
    // as-is, an absolute path gets file://, anything else resolves relative to the config
    // directory (e.g. "widgets/Foo.qml" → <configDir>/widgets/Foo.qml).
    function resolveWidgetUrl(src) {
        if (src.indexOf("qrc:") === 0 || src.indexOf("file:") === 0)
            return src
        if (src.indexOf("/") === 0)
            return "file://" + src
        var dir = (typeof configDir !== "undefined" && configDir) ? configDir : "."
        return "file://" + dir + "/" + src
    }

    function customToolId(name) {
        if (name.indexOf("CustomTool:") === 0) {
            return name.substring("CustomTool:".length)
        }
        return ""
    }

    // Surface a clear warning when an applet or a runtime custom QML widget fails to
    // load (a bad `source` path, or a QML error in a user-provided widget) — otherwise
    // the slot just stays empty silently and the author has nothing to go on.
    function reportLoadStatus(name, ldr) {
        if (ldr && ldr.status === Loader.Error) {
            console.warn("qbar: failed to load '" + name + "' from " + ldr.source
                + " — check the path and the widget's QML for errors.")
        }
    }

    function appletWidth(item) {
        if (!item) {
            return 0
        }
        // An applet that declares preferredWidth opts into Bar.qml driving its
        // slot size — trust it even when 0 (e.g. I3Mode hidden). Falling back to
        // item.width here would read a stale value: Loader's anchors.fill resize
        // clears the applet's own "width: preferredWidth" binding the first time
        // it runs, so item.width can no longer track preferredWidth back down to 0.
        if (typeof item.preferredWidth === "number") {
            return item.preferredWidth
        }
        if (typeof item.implicitWidth === "number" && item.implicitWidth > 0) {
            return item.implicitWidth
        }
        if (typeof item.width === "number" && item.width > 0) {
            return item.width
        }
        return 0
    }

    function appletHeight(item) {
        if (!item) {
            return theme.height
        }
        if (typeof item.implicitHeight === "number" && item.implicitHeight > 0) {
            return item.implicitHeight
        }
        if (typeof item.height === "number" && item.height > 0) {
            return item.height
        }
        return theme.height
    }

    function filteredApplets(list, excluded) {
        var result = []
        for (var i = 0; i < list.length; ++i) {
            if (list[i] !== excluded) {
                result.push(list[i])
            }
        }
        return result
    }

    function bindTitleWidth(slot) {
        if (slot && slot.item && slot.appletName === "Title") {
            var point = slot.mapToItem(root, 0, 0)
            slot.item.barCenterX = root.width / 2 - point.x
        }
    }

    function updateTitleGeometry() {
        if (!titleLoader.item) {
            return
        }

        titleLoader.item.barWidth = root.width
        titleLoader.item.leftOccupiedWidth = leftPane.x + leftPane.width
        titleLoader.item.rightOccupiedWidth = root.width - rightPane.x
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: root.barMarginLeft
        anchors.rightMargin: root.barMarginRight
        color: root.barBg
        radius: root.barRadius
    }

    Item {
        id: leftPane
        anchors.left: parent.left
        anchors.leftMargin: root.barMarginLeft + root.barPaddingLeft + root.styleMargin(root.leftStyle, "left")
        anchors.top: parent.top
        anchors.topMargin: root.styleMargin(root.leftStyle, "top")
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.styleMargin(root.leftStyle, "bottom")
        width: leftRow.implicitWidth + root.stylePadding(root.leftStyle, "left") + root.stylePadding(root.leftStyle, "right")

        QBar.CssFill {
            anchors.fill: parent
            style: root.leftStyle
            radius: root.styleRadius(root.leftStyle)
        }

        Behavior on width {
            NumberAnimation {
                duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                easing.type: theme.animationEasing
            }
        }

        Row {
            id: leftRow
            anchors.left: parent.left
            anchors.leftMargin: root.stylePadding(root.leftStyle, "left")
            anchors.top: parent.top
            anchors.topMargin: root.stylePadding(root.leftStyle, "top")
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.stylePadding(root.leftStyle, "bottom")
            spacing: theme.spacing

            Repeater {
                model: leftApplets

                delegate: Item {
                    id: slot
                    property string appletName: modelData
                    property int preferredWidth: 0
                    width: preferredWidth > 0 ? preferredWidth : root.appletWidth(loader.item)
                    // Fill the group's inner height so applets stay inside a
                    // vertically-inset/rounded container instead of overflowing it.
                    height: parent.height
                    onXChanged: root.bindTitleWidth(slot)

                    Behavior on width {
                        NumberAnimation {
                            duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                            easing.type: theme.animationEasing
                        }
                    }

                    Loader {
                        id: loader
                        anchors.fill: parent
                        source: appletName === "Temperature" && temperatureModel && !temperatureModel.available ? "" : root.appletUrl(appletName)
                        asynchronous: false

                        onStatusChanged: root.reportLoadStatus(appletName, loader)

                        onLoaded: {
                            if (appletName.indexOf("CustomTool:") === 0 && loader.item && "toolId" in loader.item) {
                                loader.item.toolId = root.customToolId(appletName)
                            }
                            slot.preferredWidth = root.appletWidth(loader.item)
                            root.bindTitleWidth(slot)
                        }
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

                        function onPreferredWidthUpdated(width) {
                            slot.preferredWidth = width
                            root.bindTitleWidth(slot)
                        }

                        function onActivated() {
                            if (appletName === "Clock" && barWindow) {
                                barWindow.openCalendar(loader.item)
                            } else if (appletName === "XInput" && barWindow) {
                                barWindow.cycleKeyboardLayout()
                            } else if (appletName === "Caffeine" && barWindow) {
                                barWindow.toggleCaffeine()
                            }
                        }

                        function onWorkspaceActivated(workspaceName) {
                            if (i3Ipc) {
                                i3Ipc.activateWorkspace(workspaceName)
                            }
                        }

                        function onWorkspaceScrolled(direction) {
                            if (i3Ipc) {
                                i3Ipc.activateRelativeWorkspace(direction)
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: centerPane
        anchors.left: leftPane.right
        anchors.leftMargin: root.styleMargin(root.centerStyle, "left")
        anchors.right: rightPane.left
        anchors.rightMargin: root.styleMargin(root.centerStyle, "right")
        anchors.top: parent.top
        anchors.topMargin: root.styleMargin(root.centerStyle, "top")
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.styleMargin(root.centerStyle, "bottom")
        clip: true

        QBar.CssFill {
            anchors.fill: parent
            style: root.centerStyle
            radius: root.styleRadius(root.centerStyle)
        }

        Row {
            id: centerRow
            anchors.centerIn: parent
            spacing: theme.spacing

            Repeater {
                model: root.filteredApplets(centerApplets, "Title")

                delegate: Item {
                    id: slot
                    property string appletName: modelData
                    property int preferredWidth: 0
                    width: preferredWidth > 0 ? preferredWidth : root.appletWidth(loader.item)
                    height: root.appletHeight(loader.item)

                    Behavior on width {
                        NumberAnimation {
                            duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                            easing.type: theme.animationEasing
                        }
                    }

                    Loader {
                        id: loader
                        anchors.fill: parent
                        source: appletName === "Temperature" && temperatureModel && !temperatureModel.available ? "" : root.appletUrl(appletName)
                        asynchronous: false

                        onStatusChanged: root.reportLoadStatus(appletName, loader)

                        onLoaded: {
                            if (appletName.indexOf("CustomTool:") === 0 && loader.item && "toolId" in loader.item) {
                                loader.item.toolId = root.customToolId(appletName)
                            }
                            slot.preferredWidth = root.appletWidth(loader.item)
                            root.bindTitleWidth(slot)
                        }
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

                        function onPreferredWidthUpdated(width) {
                            slot.preferredWidth = width
                            root.bindTitleWidth(slot)
                        }

                        function onActivated() {
                            if (appletName === "Clock" && barWindow) {
                                barWindow.openCalendar(loader.item)
                            } else if (appletName === "XInput" && barWindow) {
                                barWindow.cycleKeyboardLayout()
                            } else if (appletName === "Caffeine" && barWindow) {
                                barWindow.toggleCaffeine()
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: titleOverlay
        anchors.fill: parent
        z: 10
        visible: titleLoader.status === Loader.Ready

        Loader {
            id: titleLoader
            anchors.fill: parent
            asynchronous: false
            source: centerApplets.indexOf("Title") !== -1 ? root.appletUrl("Title") : ""

            onLoaded: {
                root.updateTitleGeometry()
            }
        }
    }

    Item {
        id: rightPane
        anchors.right: parent.right
        anchors.rightMargin: root.barMarginRight + root.barPaddingRight + root.styleMargin(root.rightStyle, "right")
        anchors.top: parent.top
        anchors.topMargin: root.styleMargin(root.rightStyle, "top")
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.styleMargin(root.rightStyle, "bottom")
        width: rightRow.implicitWidth + root.stylePadding(root.rightStyle, "left") + root.stylePadding(root.rightStyle, "right")

        QBar.CssFill {
            anchors.fill: parent
            style: root.rightStyle
            radius: root.styleRadius(root.rightStyle)
        }

        Behavior on width {
            NumberAnimation {
                duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                easing.type: theme.animationEasing
            }
        }

        Row {
            id: rightRow
            anchors.left: parent.left
            anchors.leftMargin: root.stylePadding(root.rightStyle, "left")
            anchors.top: parent.top
            anchors.topMargin: root.stylePadding(root.rightStyle, "top")
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.stylePadding(root.rightStyle, "bottom")
            spacing: theme.spacing

            Repeater {
                model: rightApplets

                delegate: Item {
                    id: slot
                    property string appletName: modelData
                    property int preferredWidth: 0
                    // Per-applet horizontal margins (CSS margin-left/right). Negative
                    // values pull a module toward its neighbour, letting adjacent
                    // panels (e.g. Clock + Tray) read as a single unit despite the
                    // bar's global spacing.
                    readonly property var appletStyle: (cssTheme && cssTheme.loaded && loader.item && loader.item.cssId)
                        ? cssTheme.resolve(loader.item.cssId) : ({})
                    readonly property int marginLeft: root.styleMargin(appletStyle, "left")
                    readonly property int marginRight: root.styleMargin(appletStyle, "right")
                    width: (preferredWidth > 0 ? preferredWidth : root.appletWidth(loader.item)) + marginLeft + marginRight
                    // Fill the group's inner height so applets stay inside a
                    // vertically-inset/rounded container instead of overflowing it.
                    height: parent.height

                    Behavior on width {
                        NumberAnimation {
                            duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                            easing.type: theme.animationEasing
                        }
                    }

                    Loader {
                        id: loader
                        anchors.fill: parent
                        anchors.leftMargin: slot.marginLeft
                        anchors.rightMargin: slot.marginRight
                        source: appletName === "Temperature" && temperatureModel && !temperatureModel.available ? "" : root.appletUrl(appletName)
                        asynchronous: false

                        onStatusChanged: root.reportLoadStatus(appletName, loader)

                        onLoaded: {
                            if (appletName.indexOf("CustomTool:") === 0 && loader.item && "toolId" in loader.item) {
                                loader.item.toolId = root.customToolId(appletName)
                            }
                            slot.preferredWidth = root.appletWidth(loader.item)
                        }
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

                        function onPreferredWidthUpdated(width) {
                            slot.preferredWidth = width
                        }

                        function onActivated() {
                            if (appletName === "Clock" && barWindow) {
                                barWindow.openCalendar(loader.item)
                            } else if (appletName === "XInput" && barWindow) {
                                barWindow.cycleKeyboardLayout()
                            } else if (appletName === "Caffeine" && barWindow) {
                                barWindow.toggleCaffeine()
                            }
                        }

                        function onWorkspaceActivated(workspaceName) {
                            if (i3Ipc) {
                                i3Ipc.activateWorkspace(workspaceName)
                            }
                        }

                        function onWorkspaceScrolled(direction) {
                            if (i3Ipc) {
                                i3Ipc.activateRelativeWorkspace(direction)
                            }
                        }
                    }
                }
            }
        }
    }

    onWidthChanged: updateTitleGeometry()

    Connections {
        target: leftPane
        function onWidthChanged() {
            root.updateTitleGeometry()
        }
        function onXChanged() {
            root.updateTitleGeometry()
        }
    }

    Connections {
        target: rightPane
        function onWidthChanged() {
            root.updateTitleGeometry()
        }
        function onXChanged() {
            root.updateTitleGeometry()
        }
    }
}
