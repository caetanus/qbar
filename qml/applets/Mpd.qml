import QtQuick
import "qrc:/qbar" as QBar

// Native MPD client (waybar's "mpd"): artist — title while playing/paused, hidden
// while stopped or with no MPD reachable ($MPD_HOST/$MPD_PORT, localhost:6600).
// Click toggles play/pause; scroll skips next/previous.
// CSS: #mpd { color, font-family, max-width }.
QBar.CssRect {
    id: root
    cssId: "mpd"
    height: theme.height
    width: Math.max(1, preferredWidth)

    readonly property var cssStyle: root.style

    property bool active: mpdModel ? (mpdModel.connected && mpdModel.displayText.length > 0) : false
    property string displayText: mpdModel ? mpdModel.displayText : ""
    property bool tooltipHovered: false
    readonly property int maxTextWidth: cssStyle["max-width"]
        ? cssTheme.parseLength(cssStyle["max-width"], 260) : 260
    property int preferredWidth: active ? Math.ceil(contentRow.implicitWidth + 12) : 0

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    QBar.Tooltip {
        anchorItem: root
        hovered: root.tooltipHovered && root.active
        text: mpdModel
            ? (mpdModel.artist.length > 0 ? mpdModel.artist + " — " + mpdModel.title : mpdModel.title)
              + (mpdModel.album.length > 0 ? "\n" + mpdModel.album : "")
            : ""
        side: "auto"
    }

    Row {
        id: contentRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        spacing: 5
        visible: root.active

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "♪"
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
            text: root.displayText
            elide: Text.ElideRight
            width: Math.min(implicitWidth, root.maxTextWidth)
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.tooltipHovered = containsMouse
        onClicked: if (mpdModel) mpdModel.toggle()
        onWheel: function(ev) {
            if (!mpdModel)
                return
            if (ev.angleDelta.y < 0)
                mpdModel.next()
            else if (ev.angleDelta.y > 0)
                mpdModel.previous()
        }
    }
}
