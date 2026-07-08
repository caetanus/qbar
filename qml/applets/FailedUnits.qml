import QtQuick
import "qrc:/qbar" as QBar

// Failed systemd units (waybar's "systemd-failed-units"): hidden while everything
// is healthy, a warning badge + count when a system or user unit is in the failed
// state. Click opens the unit list popup. CSS: #systemd-failed-units { color }.
QBar.CssRect {
    id: root
    cssId: "systemd-failed-units"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property int failedCount: failedUnitsModel ? failedUnitsModel.count : 0
    property string tooltipText: failedUnitsModel ? failedUnitsModel.tooltipText : ""
    property bool tooltipHovered: false
    property int preferredWidth: failedCount > 0 ? Math.ceil(contentRow.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    readonly property color warnColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"]) : "#e5c07b"

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: root.tooltipText
        side: "auto"
    }

    QBar.Popup {
        id: unitsPopup
        name: "systemd"
        anchorItem: root
        source: "qrc:/popups/SystemdPopup.qml"
        payload: ({ failedUnits: failedUnitsModel })
        popupWidth: 440
        popupHeight: Math.min(420, 90 + root.failedCount * 26)
        gap: 2
        placement: "below"
        horizontalAlignment: "right"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.failedCount > 0

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "⚠"
            color: root.warnColor
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.failedCount
            color: root.warnColor
            font.bold: true
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: unitsPopup.toggle()
    }
}
