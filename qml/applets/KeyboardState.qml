import QtQuick
import "qrc:/qbar" as QBar

// Lock-key indicators (waybar's "keyboard-state"): one badge per lock key, lit
// while active. Variants: "KeyboardState" shows caps+num; "/caps", "/num",
// "/scroll" show a single key; "/all" adds scroll lock.
// CSS: #keyboard-state { color } — the lit badge uses the accent, dimmed when off.
QBar.CssRect {
    id: root
    cssId: "keyboard-state"
    height: theme.height
    width: Math.max(1, preferredWidth)

    property string variant: ""

    readonly property var cssStyle: root.style

    property bool available: keyboardStateModel ? keyboardStateModel.available : false
    readonly property bool showCaps: variant === "" || variant === "all" || variant === "caps"
    readonly property bool showNum: variant === "" || variant === "all" || variant === "num"
    readonly property bool showScroll: variant === "all" || variant === "scroll"
    property int preferredWidth: available ? Math.ceil(contentRow.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    readonly property color litColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"]) : cssTheme.parseColor(theme.accent)
    readonly property color dimColor: {
        var c = cssTheme.parseColor(theme.foreground)
        return Qt.rgba(c.r, c.g, c.b, 0.35)
    }

    component LockBadge: Text {
        property bool lit: false
        anchors.verticalCenter: parent.verticalCenter
        color: lit ? root.litColor : root.dimColor
        font.family: cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: lit
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 6
        visible: root.available

        LockBadge {
            visible: root.showCaps
            lit: keyboardStateModel ? keyboardStateModel.capsLock : false
            text: "A⇪"
        }
        LockBadge {
            visible: root.showNum
            lit: keyboardStateModel ? keyboardStateModel.numLock : false
            text: "1⃣"
        }
        LockBadge {
            visible: root.showScroll
            lit: keyboardStateModel ? keyboardStateModel.scrollLock : false
            text: "⇳"
        }
    }
}
