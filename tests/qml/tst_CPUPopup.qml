import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "CPUPopup"
    when: windowShown

    property var theme: ({
        "height": 28,
        "fontFamily": "Sans Serif",
        "fontSize": 9,
        "foreground": Qt.rgba(0.93, 0.95, 0.98, 1.0),
        "background": Qt.rgba(0.15, 0.19, 0.22, 1.0)
    })

    QtObject {
        id: cpuModel
        property int usage: 63
        property var usageHistory: [10, 18, 32, 45, 63]
        property double loadAverage1: 1.25
        property double loadAverage5: 1.10
        property double loadAverage15: 0.95
        property var loadAverageHistory: [0.6, 0.8, 1.0, 1.25]
        property int processCount: 184
        property int runningProcesses: 3
        property var topProcesses: [
            { "pid": 101, "name": "compiler", "usage": 38.4 },
            { "pid": 102, "name": "browser", "usage": 17.9 },
            { "pid": 103, "name": "qml", "usage": 8.2 }
        ]
        property var topMemoryProcesses: [
            { "pid": 201, "name": "browser", "memoryKb": 1536000 },
            { "pid": 202, "name": "editor", "memoryKb": 524288 },
            { "pid": 203, "name": "qbar", "memoryKb": 65536 }
        ]
        property int memoryUsage: 58
        property var memoryUsageHistory: [40, 44, 49, 54, 58]
        property int swapUsage: 7
        property var swapUsageHistory: [0, 2, 4, 6, 7]
        property int coreCount: 6
        property var coreUsages: [20, 35, 48, 66, 78, 91]
        property var coreHistories: [
            [10, 14, 20],
            [20, 28, 35],
            [30, 41, 48],
            [25, 50, 66],
            [40, 66, 78],
            [55, 80, 91]
        ]
        signal coresChanged()

        function coreName(index) { return "cpu" + index }
        function coreUsage(index) { return coreUsages[index] }
        function coreHistory(index) { return coreHistories[index] }
    }

    function createPopup(properties) {
        var component = Qt.createComponent("qrc:/popups/CPUPopup.qml")
        compare(component.status, Component.Ready, component.errorString())
        var item = component.createObject(testCase, properties)
        verify(item !== null, component.errorString())
        return item
    }

    function test_cpu_mode_loads() {
        var item = createPopup({ "cpu": cpuModel, "coreColumns": 4 })
        compare(item.popupMode, "cpu")
        compare(item.width, 640)
        verify(item.implicitHeight > 0)
        item.destroy()
    }

    function test_memory_mode_loads() {
        var item = createPopup({ "cpu": cpuModel, "popupMode": "memory", "coreColumns": 4 })
        compare(item.popupMode, "memory")
        compare(item.width, 640)
        verify(item.implicitHeight > 0)
        item.destroy()
    }
}
