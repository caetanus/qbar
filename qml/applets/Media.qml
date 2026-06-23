import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: preferredWidth
    clip: true

    readonly property string cssId: "media"
    readonly property var cssStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve(cssId) : ({})
    readonly property real paddingLeft: boxSide("padding", "left", 6)
    readonly property real paddingRight: boxSide("padding", "right", 6)
    readonly property real paddingTop: boxSide("padding", "top", 0)
    readonly property real paddingBottom: boxSide("padding", "bottom", 0)
    // Margins inset the pill within its full-height bar slot so it doesn't span
    // the whole bar — vertical margins give it breathing room top/bottom.
    readonly property real marginTop: boxSide("margin", "top", 0)
    readonly property real marginBottom: boxSide("margin", "bottom", 0)
    readonly property real marginLeft: boxSide("margin", "left", 0)
    readonly property real marginRight: boxSide("margin", "right", 0)
    readonly property real contentHeight: Math.max(1, height - marginTop - marginBottom - paddingTop - paddingBottom)

    // Optional recessed "display" panel behind the title, styled via the CSS
    // `#media-label` selector (e.g. box-shadow: inset) for a Windows-media look.
    readonly property var labelStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("media-label") : ({})
    readonly property real labelPadding: cssStyle["label-padding"] ? parseFloat(cssStyle["label-padding"]) : 0

    // Hidden (width 0) when no MPRIS player is present — see [[loader-clears-width-binding]].
    readonly property bool active: mprisModel && mprisModel.available
    property int maxTitleWidth: cssStyle["max-width"] ? parseInt(cssStyle["max-width"]) : 180
    property real titleWidth: Math.min(titleText.contentWidth, maxTitleWidth)
    property int preferredWidth: active ? Math.ceil(marginLeft + paddingLeft + glyph.implicitWidth + contentRow.spacing + titleWidth + labelPadding * 2 + paddingRight + marginRight) : 0
    property bool tooltipHovered: false

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function boxSide(prop, side, fallback) {
        var style = cssStyle
        if (!style) {
            return fallback
        }
        var explicit = style[prop + "-" + side]
        if (explicit) {
            return parseFloat(explicit)
        }
        var shorthand = style[prop]
        if (!shorthand) {
            return fallback
        }
        var parts = String(shorthand).trim().split(/\s+/)
        if (parts.length === 1) return parseFloat(parts[0])
        if (parts.length === 2) return (side === "top" || side === "bottom") ? parseFloat(parts[0]) : parseFloat(parts[1])
        if (parts.length === 3) {
            if (side === "top") return parseFloat(parts[0])
            if (side === "bottom") return parseFloat(parts[2])
            return parseFloat(parts[1])
        }
        if (side === "top") return parseFloat(parts[0])
        if (side === "right") return parseFloat(parts[1])
        if (side === "bottom") return parseFloat(parts[2])
        return parseFloat(parts[3])
    }

    // Jump to the window emitting the media (across workspaces). Windowless
    // players (mpd and other terminal/headless services) match no window, so
    // this does nothing for them.
    function jumpToPlayer() {
        if (!mprisModel || !i3Ipc) {
            return
        }
        i3Ipc.activateWindowByPid(mprisModel.activePid())
    }

    QBar.CssFill {
        anchors.fill: parent
        anchors.topMargin: root.marginTop
        anchors.bottomMargin: root.marginBottom
        anchors.leftMargin: root.marginLeft
        anchors.rightMargin: root.marginRight
        style: root.cssStyle
        radius: cssStyle["border-radius"] ? parseFloat(cssStyle["border-radius"]) : 0
        defaultColor: cssStyle["background-color"] ? cssTheme.parseColor(cssStyle["background-color"]) : "transparent"
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

    Item {
        id: contentClip
        anchors.left: parent.left
        anchors.leftMargin: root.marginLeft + root.paddingLeft
        anchors.right: parent.right
        anchors.rightMargin: root.marginRight + root.paddingRight
        anchors.top: parent.top
        anchors.topMargin: root.marginTop + root.paddingTop
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.marginBottom + root.paddingBottom
        clip: true

        Row {
            id: contentRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            height: root.contentHeight
            spacing: 6

            Text {
                id: glyph
                height: parent.height
                verticalAlignment: Text.AlignVCenter
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

            Item {
                id: titleCell
                anchors.verticalCenter: parent.verticalCenter
                width: titleText.width + root.labelPadding * 2
                height: parent.height

                // Recessed display panel (CSS #media-label, e.g. inset box-shadow).
                QBar.CssFill {
                    anchors.fill: parent
                    style: root.labelStyle
                    radius: root.labelStyle["border-radius"] ? cssTheme.parseLength(root.labelStyle["border-radius"], 0) : 0
                    defaultColor: "transparent"
                }

                QBar.MarqueeText {
                    id: titleText
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: root.labelPadding
                    width: Math.min(root.titleWidth, Math.max(0, contentClip.width - glyph.implicitWidth - contentRow.spacing - root.labelPadding * 2))
                    height: parent.height
                    text: mprisModel ? (mprisModel.title.length > 0 ? mprisModel.title : mprisModel.playerName) : ""
                    color: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
                    font.family: cssStyle["font-family"] || theme.fontFamily
                    font.pointSize: theme.fontSize
                }
            }
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
        name: "media"
        anchorItem: root
        source: "qrc:/popups/MediaPopup.qml"
        popupWidth: 340
        popupHeight: 136
        horizontalAlignment: "center"
    }
}
