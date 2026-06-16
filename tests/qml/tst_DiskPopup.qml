import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "DiskPopup"
    when: windowShown

    property var theme: ({
        "height": 28,
        "fontFamily": "Sans Serif",
        "fontSize": 9,
        "foreground": Qt.rgba(0.93, 0.95, 0.98, 1.0),
        "background": Qt.rgba(0.15, 0.19, 0.22, 1.0)
    })

    QtObject {
        id: diskModel
        property var mounts: [
            { "path": "/", "percent": 42 },
            { "path": "/home", "percent": 67 },
            { "path": "/mnt/archive", "percent": 81 }
        ]
    }

    function test_loads_mount_pies() {
        var component = Qt.createComponent("qrc:/popups/DiskPopup.qml")
        compare(component.status, Component.Ready, component.errorString())
        var item = component.createObject(testCase, { "disk": diskModel, "columns": 2 })
        verify(item !== null, component.errorString())
        compare(item.mounts.length, 3)
        compare(item.columns, 2)
        verify(item.implicitHeight > 0)
        item.destroy()
    }
}
