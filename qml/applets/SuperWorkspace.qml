import QtQuick
import "qrc:/qbar" as QBar

// Super workspace indicator (our i3 fork): shows the active super workspace as
// "[name]" — e.g. "[1:acme]" — and collapses to nothing when there is only one
// super workspace. Left click switches back and forth, the wheel cycles through
// the known super workspaces. Gets the "urgent" class while a window of another
// super workspace is urgent. Styled by the theme's #super-workspace rule.
QBar.CssRect {
    id: root
    cssId: "super-workspace"
    cssClass: othersUrgent ? ["urgent"] : []
    height: theme.height

    readonly property var cssStyle: root.style
    readonly property var current: i3Ipc && i3Ipc.superWorkspace ? i3Ipc.superWorkspace : ({})
    readonly property var all: i3Ipc && i3Ipc.superWorkspaces ? i3Ipc.superWorkspaces : []
    readonly property bool active: all.length > 1 && current.name !== undefined
    readonly property string label: active ? "[" + current.name + "]" : ""
    readonly property bool othersUrgent: {
        for (let i = 0; i < all.length; i++) {
            if (!all[i].focused && all[i].urgent)
                return true
        }
        return false
    }

    // Sizing contract (see Scratchpad.qml): drive preferredWidth and collapse
    // to 0 when there is nothing to show.
    property int preferredWidth: active ? Math.ceil(text.implicitWidth) + 14 : 0
    width: preferredWidth
    clip: true
    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function cycle(direction) {
        if (!i3Ipc || all.length < 2)
            return
        let index = 0
        for (let i = 0; i < all.length; i++) {
            if (all[i].focused)
                index = i
        }
        const target = all[(index + direction + all.length) % all.length]
        i3Ipc.runCommand("super_workspace number " + target.num)
    }

    QBar.CssText {
        id: text
        cssId: "super-workspace"
        anchors.centerIn: parent
        visible: root.active
        text: root.label
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (i3Ipc) i3Ipc.runCommand("super_workspace back_and_forth")
        onWheel: wheel => root.cycle(wheel.angleDelta.y > 0 ? -1 : 1)
    }
}
