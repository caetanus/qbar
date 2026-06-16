import QtQuick
import QtTest
import "../../qml/applets" as Applets

TestCase {
    id: testCase
    name: "Applets"
    when: windowShown

    property var theme: ({
        "height": 28,
        "fontFamily": "Sans Serif",
        "fontSize": 9,
        "foreground": "#eef2f7",
        "background": "#263039af",
        "accent": "#63b3ed",
        "trayItemPadding": 2
    })

    QtObject {
        id: cssTheme
        property bool loaded: false
        function resolve() { return ({}) }
        function resolveWith() { return ({}) }
        function parseColor(value) { return value ? value : "#00000000" }
    }

    QtObject {
        id: qbarPopups
        property int openedCount: 0
        signal popupClosed(string id)
        function openPopup(source, properties, x, y, width, height, requestedId) {
            openedCount += 1
            return requestedId && requestedId.length > 0 ? requestedId : "popup-" + openedCount
        }
        function updatePopup(id, properties) {}
        function updateTooltip(id, properties) {}
        function closePopup(id) { popupClosed(id) }
        function closeTooltip(id) { popupClosed(id) }
        function setTooltipHovered(id, hovered) {}
    }

    QtObject {
        id: i3Ipc
        property string currentWindowTitle: "Focused window"
        property string currentKeyboardLayout: "br"
        property string bindingMode: "default"
    }

    QtObject {
        id: cpuModel
        property int usage: 37
        property var usageHistory: [10, 20, 37]
        property int memoryUsage: 52
        property var memoryUsageHistory: [40, 48, 52]
        property var swapUsageHistory: [0, 1, 3]
        property int coreCount: 4
        property double loadAverage1: 1.25
        signal memoryStatsChanged()
    }

    QtObject {
        id: networkModel
        property double downloadRateBytesPerSecond: 2097152
        property double uploadRateBytesPerSecond: 524288
        property double totalRateBytesPerSecond: 2621440
        property var downloadRateHistory: [128, 4096, 2097152]
        property var uploadRateHistory: [64, 2048, 524288]
        signal statsChanged()
    }

    QtObject {
        id: diskModel
        property bool available: true
        property int percent: 48
        property string path: "/"
        property string displayText: "48%"
        property string tooltipText: "/\n52 GB free of 100 GB"
        property var mounts: [
            { "path": "/", "percent": 48 },
            { "path": "/home", "percent": 71 }
        ]
    }

    QtObject {
        id: batteryModel
        property int capacity: 82
        property bool charging: false
        property bool full: false
    }

    QtObject {
        id: soundModel
        property bool available: true
        property bool muted: false
        property int volume: 74
        property string displayText: "74%"
        property string outputTooltipText: "Speaker"
        property bool sourceAvailable: true
        property bool sourceMuted: false
        property int sourceVolume: 62
        property string sourceDisplayText: "62%"
        property string sourceTooltipText: "Microphone"
        property string outputIconName: ""
        property string inputIconName: ""
        function toggleMute() { muted = !muted }
        function toggleSourceMute() { sourceMuted = !sourceMuted }
        function stepUp(value) { volume += value }
        function stepDown(value) { volume -= value }
        function stepSourceUp(value) { sourceVolume += value }
        function stepSourceDown(value) { sourceVolume -= value }
    }

    QtObject {
        id: brightnessModel
        property int percent: 90
        property bool available: true
        function stepUp(value) { percent += value }
        function stepDown(value) { percent -= value }
    }

    QtObject {
        id: caffeineModel
        property bool active: false
        function toggle() { active = !active }
    }

    QtObject {
        id: temperatureModel
        property string displayText: "42°C"
        property string tooltipText: "CPU 42°C"
        property bool available: true
    }

    QtObject {
        id: networkManagerModel
        property string mode: "wireless"
        property string iconName: "network-wireless-signal-excellent-symbolic"
        property string label: "93%"
        property string interfaceName: "wlan0"
        property string ipText: "192.168.1.10"
        property string ipv4Text: "192.168.1.10"
        property string ipv6Text: "fe80::1"
        property string tooltipText: "wlan0 | lab | 93% | ch 11 | 5G | 866Mb/s"
        property int strength: 93
        signal statusChanged()
    }

    ListModel {
        id: trayModel
        ListElement {
            title: "Tray"
            status: "Active"
            iconSource: ""
            symbolicIconSource: ""
            overlayIconName: ""
            overlaySymbolicIconSource: ""
        }
        function activateAt(index, x, y) {}
        function secondaryActivate(index) {}
        function contextMenuAt(index, x, y) {}
        function scroll(index, delta, orientation) {}
    }

    property var workspaceModel: null
    property var customTools: ({
        "custom/test": {
            "command": "",
            "interval": 60,
            "return-type": "json",
            "format": "{} ({percentage}%)",
            "format-icons": {
                "up": "▲",
                "down": "▼"
            }
        }
    })

    Item {
        id: harness
        width: 900
        height: 560

        Applets.Workspaces { id: workspaces; y: 0 }
        Applets.I3Mode { id: i3Mode; y: 32 }
        Applets.CPU { id: cpu; y: 64 }
        Applets.Memory { id: memory; y: 96 }
        Applets.Network { id: network; y: 128 }
        Applets.Title { id: title; y: 160; barWidth: 420 }
        Applets.Caffeine { id: caffeine; y: 192 }
        Applets.Brightness { id: brightness; y: 224 }
        Applets.XInput { id: xinput; y: 256 }
        Applets.NetworkManager { id: networkManager; y: 288 }
        Applets.Temperature { id: temperature; y: 320 }
        Applets.Sound { id: sound; y: 352 }
        Applets.Battery { id: battery; y: 384 }
        Applets.Disk { id: disk; y: 416 }
        Applets.Clock { id: clock; y: 448 }
        Applets.Tray { id: tray; y: 480 }
        Applets.CustomTool { id: customTool; y: 512; toolId: "custom/test" }
    }

    function assertApplet(item, name) {
        verify(item !== null, name + " should exist")
        compare(item.height, theme.height, name + " should use bar height")
        verify(item.width >= 1, name + " should have a positive width")
    }

    function test_all_applets_load() {
        assertApplet(workspaces, "Workspaces")
        assertApplet(cpu, "CPU")
        assertApplet(memory, "Memory")
        assertApplet(network, "Network")
        assertApplet(title, "Title")
        assertApplet(caffeine, "Caffeine")
        assertApplet(brightness, "Brightness")
        assertApplet(xinput, "XInput")
        assertApplet(networkManager, "NetworkManager")
        assertApplet(temperature, "Temperature")
        assertApplet(sound, "Sound")
        assertApplet(battery, "Battery")
        assertApplet(disk, "Disk")
        assertApplet(clock, "Clock")
        assertApplet(tray, "Tray")
        assertApplet(customTool, "CustomTool")
    }

    function test_key_interactions_do_not_throw() {
        caffeineModel.active = false
        caffeineModel.toggle()
        verify(caffeineModel.active)
        compare(network.formatRate(900 * 1024), "0.9 M/s")
        compare(network.formatRate(800), "0.8 K/s")
        clock.setFormatIndex(clock.formatIndex + 1)
        verify(clock.width >= 1)
    }

    function test_custom_tool_reads_format_from_config() {
        compare(customTool.toolModel.format, "{} ({percentage}%)",
            "CustomTool should read 'format' from customTools config")
        compare(customTool.toolModel.formatIcons["up"], "▲",
            "CustomTool should read 'format-icons' from customTools config")
    }

    function test_i3_mode_hidden_in_default_mode() {
        i3Ipc.bindingMode = "default"
        compare(i3Mode.height, theme.height, "I3Mode should use bar height")
        compare(i3Mode.active, false, "I3Mode should be inactive in the default binding mode")
        compare(i3Mode.width, 0, "I3Mode should occupy no width in the default binding mode")
    }

    function test_i3_mode_visible_in_resize_mode() {
        i3Ipc.bindingMode = "resize"
        compare(i3Mode.modeName, "resize", "I3Mode should reflect i3Ipc.bindingMode")
        compare(i3Mode.active, true, "I3Mode should be active outside of the default binding mode")
        verify(i3Mode.width >= 1, "I3Mode should have a positive width outside of the default binding mode")

        i3Ipc.bindingMode = "default"
        compare(i3Mode.modeName, "default", "I3Mode should reflect i3Ipc.bindingMode going back to default")
        compare(i3Mode.active, false, "I3Mode should become inactive again after leaving the binding mode")
        compare(i3Mode.preferredWidth, 0, "I3Mode preferredWidth should return to 0 after leaving the binding mode")
        compare(i3Mode.width, 0, "I3Mode should occupy no width again after leaving the binding mode")
    }
}
