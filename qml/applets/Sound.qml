import QtQuick
import "qrc:/qbar" as QBar

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    property bool outputAvailable: soundModel ? soundModel.available : false
    property bool outputMuted: soundModel ? soundModel.muted : false
    property int outputVolume: soundModel ? soundModel.volume : 0
    property string outputText: soundModel ? soundModel.displayText : "--"
    property string outputTooltipText: soundModel ? soundModel.outputTooltipText : "sound unavailable"
    property bool inputAvailable: soundModel ? soundModel.sourceAvailable : false
    property bool inputMuted: soundModel ? soundModel.sourceMuted : false
    property int inputVolume: soundModel ? soundModel.sourceVolume : 0
    property string inputText: soundModel ? soundModel.sourceDisplayText : "--"
    property string inputTooltipText: soundModel ? soundModel.sourceTooltipText : "mic unavailable"
    property bool outputTooltipHovered: false
    property bool inputTooltipHovered: false
    property color outputBackground: root.outputAvailable ? (root.outputMuted ? "#59616d" : "#f0c808") : "#2b2f33"
    property color inputBackground: root.inputAvailable ? (root.inputMuted ? "#59616d" : "#e59b0f") : "#2b2f33"
    property int preferredWidth: Math.ceil(contentRow.implicitWidth + 10)

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    function contrastTextColor(background) {
        var luminance = (0.2126 * background.r) + (0.7152 * background.g) + (0.0722 * background.b)
        return luminance < 0.5 ? "#ffffff" : "#171717"
    }

    function contrastIconColor(background) {
        var luminance = (0.2126 * background.r) + (0.7152 * background.g) + (0.0722 * background.b)
        return luminance < 0.5 ? "#ffffff" : "#111111"
    }

    Row {
        id: contentRow
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        Rectangle {
            id: outputBlock
            width: outputRow.implicitWidth + 10
            height: theme.height
            color: root.outputBackground

            Row {
                id: outputRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 6
                spacing: 4

                Item {
                    width: 18
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: outputIcon
                        anchors.fill: parent
                        source: root.outputMuted
                            ? "image://themeicon/audio-volume-muted-symbolic|" + root.contrastIconColor(root.outputBackground)
                            : (root.outputVolume < 34
                                ? "image://themeicon/audio-volume-low-symbolic|" + root.contrastIconColor(root.outputBackground)
                                : (root.outputVolume < 67
                                    ? "image://themeicon/audio-volume-medium-symbolic|" + root.contrastIconColor(root.outputBackground)
                                    : "image://themeicon/audio-volume-high-symbolic|" + root.contrastIconColor(root.outputBackground)))
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }

                    Canvas {
                        anchors.fill: parent
                        visible: outputIcon.status !== Image.Ready
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            ctx.strokeStyle = root.contrastIconColor(root.outputBackground)
                            ctx.fillStyle = root.contrastIconColor(root.outputBackground)
                            ctx.lineWidth = 1.6
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"

                            ctx.beginPath()
                            ctx.moveTo(2.3, 6.6)
                            ctx.lineTo(5.1, 6.6)
                            ctx.lineTo(7.3, 4.7)
                            ctx.lineTo(7.3, 11.3)
                            ctx.lineTo(5.1, 9.4)
                            ctx.lineTo(2.3, 9.4)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.contrastTextColor(root.outputBackground)
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    text: root.outputText
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onContainsMouseChanged: root.outputTooltipHovered = containsMouse
                onClicked: {
                    if (soundModel && root.outputAvailable) {
                        soundModel.toggleMute()
                    }
                }
                onWheel: function(wheel) {
                    if (!soundModel || !root.outputAvailable) {
                        return
                    }
                    if (wheel.angleDelta.y > 0) {
                        soundModel.stepUp(5)
                    } else if (wheel.angleDelta.y < 0) {
                        soundModel.stepDown(5)
                    }
                }
            }

            QBar.Tooltip {
                anchorItem: outputBlock
                hovered: root.outputTooltipHovered
                text: root.outputTooltipText
                side: "auto"
            }
        }

        Rectangle {
            id: inputBlock
            width: inputRow.implicitWidth + 10
            height: theme.height
            color: root.inputBackground

            Row {
                id: inputRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 6
                spacing: 4

                Item {
                    width: 18
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: inputIcon
                        anchors.fill: parent
                        source: root.inputMuted
                            ? "image://themeicon/audio-input-microphone-muted-symbolic|" + root.contrastIconColor(root.inputBackground)
                            : (root.inputVolume < 34
                                ? "image://themeicon/audio-input-microphone-low-symbolic|" + root.contrastIconColor(root.inputBackground)
                                : (root.inputVolume < 67
                                    ? "image://themeicon/audio-input-microphone-medium-symbolic|" + root.contrastIconColor(root.inputBackground)
                                    : "image://themeicon/audio-input-microphone-high-symbolic|" + root.contrastIconColor(root.inputBackground)))
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }

                    Canvas {
                        anchors.fill: parent
                        visible: inputIcon.status !== Image.Ready
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            ctx.strokeStyle = root.contrastIconColor(root.inputBackground)
                            ctx.fillStyle = root.contrastIconColor(root.inputBackground)
                            ctx.lineWidth = 1.5
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"

                            ctx.beginPath()
                            ctx.moveTo(8.0, 3.0)
                            ctx.bezierCurveTo(6.4, 3.0, 5.3, 4.1, 5.3, 5.7)
                            ctx.lineTo(5.3, 8.0)
                            ctx.bezierCurveTo(5.3, 9.7, 6.5, 10.9, 8.0, 10.9)
                            ctx.bezierCurveTo(9.5, 10.9, 10.7, 9.7, 10.7, 8.0)
                            ctx.lineTo(10.7, 5.7)
                            ctx.bezierCurveTo(10.7, 4.1, 9.6, 3.0, 8.0, 3.0)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.contrastTextColor(root.inputBackground)
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize
                    text: root.inputText
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onContainsMouseChanged: root.inputTooltipHovered = containsMouse
                onClicked: {
                    if (soundModel && root.inputAvailable) {
                        soundModel.toggleSourceMute()
                    }
                }
                onWheel: function(wheel) {
                    if (!soundModel || !root.inputAvailable) {
                        return
                    }
                    if (wheel.angleDelta.y > 0) {
                        soundModel.stepSourceUp(5)
                    } else if (wheel.angleDelta.y < 0) {
                        soundModel.stepSourceDown(5)
                    }
                }
            }

            QBar.Tooltip {
                anchorItem: inputBlock
                hovered: root.inputTooltipHovered
                text: root.inputTooltipText
                side: "auto"
            }
        }
    }
}
