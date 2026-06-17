import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    width: implicitWidth
    height: implicitHeight
    implicitWidth: label.implicitWidth + 20
    implicitHeight: Math.max(28, label.implicitHeight + 12)

    property string text: ""

    // Themeable via the CSS `#tooltip` selector (background, border, color,
    // border-radius, box-shadow, font-family/weight). Falls back to the default
    // dark chip when the theme says nothing.
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("tooltip") : ({})

    function boldFromWeight(weight) {
        if (weight === undefined) {
            return true
        }
        var w = String(weight).toLowerCase()
        return w === "bold" || w === "bolder" || parseInt(w) >= 600
    }

    QBar.CssFill {
        anchors.fill: parent
        style: root.cssStyle
        radius: root.cssStyle["border-radius"] ? cssTheme.parseLength(root.cssStyle["border-radius"], 3) : 3
        defaultColor: "#24303a"
        defaultBorderColor: Qt.rgba(1, 1, 1, 0.15)
        defaultBorderWidth: 1
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: root.cssStyle["color"] ? cssTheme.parseColor(root.cssStyle["color"]) : "#ffffff"
        font.family: root.cssStyle["font-family"] || theme.fontFamily
        font.pointSize: theme.fontSize
        font.bold: root.boldFromWeight(root.cssStyle["font-weight"])
        text: root.text
    }
}
