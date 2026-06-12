import QtQuick

Item {
    id: root
    anchors.fill: parent
    clip: true

    function appletUrl(name) {
        return "qrc:/applets/" + name + ".qml"
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

    function bindTitleWidth(slot) {
        if (slot && slot.item && slot.appletName === "Title") {
            var point = slot.mapToItem(root, 0, 0)
            slot.item.barCenterX = root.width / 2 - point.x
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    Item {
        id: leftPane
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: leftRow.implicitWidth

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
                    width: root.appletWidth(loader.item)
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

                        onLoaded: root.bindTitleWidth(slot)
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

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
                model: centerApplets

                delegate: Item {
                    id: slot
                    property string appletName: modelData
                    width: root.appletWidth(loader.item)
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

                        onLoaded: root.bindTitleWidth(slot)
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

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
        id: rightPane
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: rightRow.implicitWidth

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
                    width: root.appletWidth(loader.item)
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
                    }

                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

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
}
