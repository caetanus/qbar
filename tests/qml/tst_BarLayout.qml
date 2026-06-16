import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "BarLayout"
    when: windowShown
    width: 400
    height: 100

    property var theme: ({
        "height": 28,
        "fontFamily": "Sans Serif",
        "fontSize": 9,
        "animationDuration": 0,
        "animationEasing": Easing.Linear
    })

    QtObject {
        id: cssTheme
        property bool loaded: false
        function resolve() { return ({}) }
        function resolveWith() { return ({}) }
        function parseColor(value) { return value ? value : "#00000000" }
    }

    QtObject {
        id: i3Ipc
        property string bindingMode: "default"
    }

    function appletWidth(item) {
        if (!item) {
            return 0
        }
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

    Row {
        id: row
        spacing: 2

        Rectangle { id: before; width: 30; height: 20; color: "red" }

        Item {
            id: slot
            property int preferredWidth: 0
            width: preferredWidth > 0 ? preferredWidth : testCase.appletWidth(loader.item)
            height: 20

            Loader {
                id: loader
                anchors.fill: parent
                source: "../../qml/applets/I3Mode.qml"
                asynchronous: false

                onLoaded: {
                    slot.preferredWidth = testCase.appletWidth(loader.item)
                }
            }

            Connections {
                target: loader.item
                ignoreUnknownSignals: true

                function onPreferredWidthUpdated(width) {
                    slot.preferredWidth = width
                }
            }
        }

        Rectangle { id: after; width: 30; height: 20; color: "green" }
    }

    function test_slot_collapses_after_leaving_resize_mode() {
        compare(slot.preferredWidth, 0, "slot.preferredWidth should start at 0")
        compare(slot.width, 0, "slot.width should start at 0")
        var xWhenHidden = after.x

        i3Ipc.bindingMode = "resize"
        wait(50)
        verify(slot.preferredWidth > 0, "slot.preferredWidth should grow in resize mode")
        verify(slot.width > 0, "slot.width should grow in resize mode")
        var xWhenShown = after.x
        verify(xWhenShown > xWhenHidden, "after.x should shift right when slot grows: hidden=" + xWhenHidden + " shown=" + xWhenShown)

        i3Ipc.bindingMode = "default"
        wait(50)
        compare(loader.item.preferredWidth, 0, "I3Mode.preferredWidth should return to 0")
        compare(loader.item.width, 0, "I3Mode.width should return to 0")
        compare(slot.preferredWidth, 0, "slot.preferredWidth should return to 0 after leaving resize mode")
        compare(slot.width, 0, "slot.width should return to 0 after leaving resize mode")
        compare(after.x, xWhenHidden, "after.x should return to original position after leaving resize mode")
    }
}
