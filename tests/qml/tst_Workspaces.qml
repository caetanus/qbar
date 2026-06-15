import QtQuick
import QtTest
import "../../qml/applets" as Applets

TestCase {
    id: testCase
    name: "Workspaces"
    when: windowShown

    property var theme: ({
        "height": 28,
        "fontFamily": "Sans Serif",
        "fontSize": 9,
        "accent": "#63b3ed"
    })
    property var workspaceModel: null
    property var cssTheme: null

    Item {
        id: harness
        width: subject.width
        height: 28

        Applets.Workspaces {
            id: subject
        }
    }

    function test_fallbackLoads() {
        verify(subject.width > 0)
        compare(subject.height, 28)
    }

    function test_scrollSignal() {
        let observed = 0
        subject.workspaceScrolled.connect(function(direction) {
            observed = direction
        })
        subject.scrollWorkspace({ angleDelta: { y: -120 }, accepted: false })
        compare(observed, 1)
    }
}
