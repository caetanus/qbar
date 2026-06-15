import QtQuick

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
        if (parts.length === 2) return parseFloat(parts[1]) // vertical horizontal
        if (parts.length === 3) return parseFloat(parts[1]) // top horizontal bottom
        return side === "left" ? parseFloat(parts[3]) : parseFloat(parts[1]) // top right bottom left
    }
    readonly property real barPaddingLeft: root.paddingSide(barStyle, "left")
    readonly property real barPaddingRight: root.paddingSide(barStyle, "right")

    function appletUrl(name) {
        if (name.indexOf("CustomTool:") === 0) {
            return "qrc:/applets/CustomTool.qml"
        }
        return "qrc:/applets/" + name + ".qml"
    }

    function customToolId(name) {
        if (name.indexOf("CustomTool:") === 0) {
            return name.substring("CustomTool:".length)
        }
        return ""
    }

    function appletWidth(item) {
        if (!item) {
            return 0
        }
        if (typeof item.preferredWidth === "number" && item.preferredWidth > 0) {
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
        anchors.leftMargin: root.barMarginLeft + root.barPaddingLeft
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: leftRow.implicitWidth

        Behavior on width {
            NumberAnimation {
                duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                easing.type: theme.animationEasing
            }
        }

        Row {
            id: leftRow
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: theme.spacing

            Repeater {
                model: leftApplets

                delegate: Item {
                    id: slot
                    property string appletName: modelData
                    property int preferredWidth: 0
                    width: preferredWidth > 0 ? preferredWidth : root.appletWidth(loader.item)
                    height: root.appletHeight(loader.item)
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
        anchors.right: rightPane.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true

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
        anchors.rightMargin: root.barMarginRight + root.barPaddingRight
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: rightRow.implicitWidth

        Behavior on width {
            NumberAnimation {
                duration: theme.animationDuration > 0 ? theme.animationDuration : 200
                easing.type: theme.animationEasing
            }
        }

        Row {
            id: rightRow
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: theme.spacing

            Repeater {
                model: rightApplets

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
