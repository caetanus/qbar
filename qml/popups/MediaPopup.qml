import QtQuick

Item {
    id: root
    implicitWidth: 340
    implicitHeight: 136

    readonly property var popupStyle: cssTheme && cssTheme.loaded ? cssTheme.resolve("popup") : ({})
    readonly property color fg: popupStyle["color"] ? cssTheme.parseColor(popupStyle["color"]) : theme.foreground

    Row {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 14

        // Cover art (falls back to a music glyph when no artUrl).
        Rectangle {
            id: cover
            width: 92
            height: 92
            anchors.verticalCenter: parent.verticalCenter
            radius: 6
            clip: true
            color: Qt.rgba(1, 1, 1, 0.08)

            Image {
                anchors.fill: parent
                source: mprisModel && mprisModel.artUrl ? mprisModel.artUrl : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

            Text {
                anchors.centerIn: parent
                visible: !(mprisModel && mprisModel.artUrl)
                text: "♪"
                color: root.fg
                opacity: 0.6
                font.pointSize: theme.fontSize + 14
            }
        }

        Column {
            width: parent.width - cover.width - 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                width: parent.width
                elide: Text.ElideRight
                text: mprisModel ? mprisModel.playerName : ""
                color: root.fg
                opacity: 0.65
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize - 1
            }

            Text {
                width: parent.width
                elide: Text.ElideRight
                text: mprisModel && mprisModel.title.length > 0 ? mprisModel.title : qsTr("Nothing playing")
                color: root.fg
                font.bold: true
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize + 2
            }

            Text {
                width: parent.width
                elide: Text.ElideRight
                visible: mprisModel && mprisModel.artist.length > 0
                text: mprisModel ? mprisModel.artist : ""
                color: root.fg
                opacity: 0.8
                font.family: theme.fontFamily
                font.pointSize: theme.fontSize
            }

            Row {
                spacing: 18
                topPadding: 6

                component Control: Text {
                    property bool can: true
                    color: root.fg
                    opacity: can ? 1.0 : 0.3
                    font.pointSize: theme.fontSize + 6
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        enabled: parent.can
                        onClicked: parent.clicked()
                    }
                    signal clicked()
                }

                Control {
                    text: "⏮"
                    can: mprisModel && mprisModel.canGoPrevious
                    onClicked: mprisModel.previous()
                }
                Control {
                    text: mprisModel && mprisModel.playing ? "⏸" : "▶"
                    can: mprisModel && (mprisModel.canPlay || mprisModel.canPause)
                    onClicked: mprisModel.playPause()
                }
                Control {
                    text: "⏭"
                    can: mprisModel && mprisModel.canGoNext
                    onClicked: mprisModel.next()
                }
            }
        }
    }
}
