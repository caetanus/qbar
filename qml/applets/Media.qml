import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: preferredWidth
    clip: true

    readonly property string cssId: "media"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})

    // Hidden (width 0) when no MPRIS player is present — see [[loader-clears-width-binding]].
    readonly property bool active: mprisModel && mprisModel.available
    property int maxTitleWidth: cssStyle["max-width"] ? parseInt(cssStyle["max-width"]) : 180
    property real titleWidth: Math.min(titleText.contentWidth, maxTitleWidth)
    property int preferredWidth: active ? Math.ceil(6 + glyph.implicitWidth + 6 + titleWidth + 6) : 0
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // Jump to the window emitting the media (across workspaces). Windowless
    // players (mpd and other terminal/headless services) match no window, so
    // this does nothing for them.
    function jumpToPlayer() {
        if (!mprisModel || !i3Ipc) {
            return
        }
        i3Ipc.activateWindowByPid(mprisModel.activePid())
    }

    Rectangle {
        anchors.fill: parent
        color: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "transparent"
    }

    // Below contentRow so the play/pause glyph wins its own clicks; clicks on the
    // title or empty space fall through here to open the popup.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                mediaPopup.toggle()
            } else {
                root.jumpToPlayer()
            }
        }
        onWheel: function(wheel) {
            if (!mprisModel) {
                return
            }
            if (wheel.angleDelta.y > 0) {
                mprisModel.previous()
            } else {
                mprisModel.next()
            }
        }
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 6

        Text {
            id: glyph
            anchors.verticalCenter: parent.verticalCenter
            text: mprisModel && mprisModel.playing ? "⏸" : "▶" // pause : play
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize

            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                cursorShape: Qt.PointingHandCursor
                enabled: mprisModel && (mprisModel.canPlay || mprisModel.canPause)
                onClicked: if (mprisModel) mprisModel.playPause()
            }
        }

        QBar.MarqueeText {
            id: titleText
            anchors.verticalCenter: parent.verticalCenter
            width: root.titleWidth
            height: root.height
            text: mprisModel ? (mprisModel.title.length > 0 ? mprisModel.title : mprisModel.playerName) : ""
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered
        text: mprisModel
            ? (mprisModel.title + (mprisModel.artist.length > 0 ? "\n" + mprisModel.artist : ""))
            : ""
    }

    QBar.Popup {
        id: mediaPopup
        anchorItem: root
        source: "qrc:/popups/MediaPopup.qml"
        popupWidth: 340
        popupHeight: 136
        horizontalAlignment: "center"
    }
}
