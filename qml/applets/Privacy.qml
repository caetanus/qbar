import QtQuick
import "qrc:/qbar" as QBar

// waybar's "privacy" module: a microphone / camera indicator that appears only while
// something is capturing (PipeWire). Hidden (width 0) when idle; hover for the apps in use.
QBar.CssRect {
    id: root
    cssId: "privacy"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style
    readonly property bool active: privacyModel ? privacyModel.active : false
    readonly property bool micActive: privacyModel ? privacyModel.micActive : false
    readonly property bool cameraActive: privacyModel ? privacyModel.cameraActive : false
    property bool tooltipHovered: false

    property int preferredWidth: active ? Math.ceil(row.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: privacyModel ? privacyModel.tooltipText : ""
        side: "auto"
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 6
        visible: root.active

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.micActive
            text: "🎤"
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.cameraActive
            text: "📹"
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: root.tooltipHovered = containsMouse
    }
}
