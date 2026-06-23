import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

Item {
    id: root
    height: theme.height
    width: Math.max(1, preferredWidth)

    // CSS id matches waybar's pulseaudio module
    readonly property string cssId: "pulseaudio"

    // "/variant" from the module name (Bar.qml sets it): "" shows both pills, "out" shows
    // only the speaker, "in" only the mic. Lets `Sound/out` + `Sound/in` be placed
    // separately (e.g. the mic tucked into a drawer) while staying one applet.
    property string variant: ""
    readonly property bool showOutput: root.variant !== "in"
    readonly property bool showInput: root.variant !== "out"
    // Output section CSS (muted class when muted, unavailable when not available)
    readonly property var outputCssStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve(cssId, outputAvailable ? (outputMuted ? ["muted"] : []) : ["unavailable"])
        : ({})
    // Input/source section CSS
    readonly property var inputCssStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve(cssId, inputAvailable ? (inputMuted ? ["source-muted"] : ["source"]) : ["unavailable"])
        : ({})

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
    property string outputIconName: soundModel ? soundModel.outputIconName : ""
    property string inputIconName: soundModel ? soundModel.inputIconName : ""

    property color outputBackground: {
        if (outputCssStyle["background-color"]) return cssTheme.parseColor(outputCssStyle["background-color"])
        return root.outputAvailable ? (root.outputMuted ? "#59616d" : "#f0c808") : "#2b2f33"
    }
    property color inputBackground: {
        if (inputCssStyle["background-color"]) return cssTheme.parseColor(inputCssStyle["background-color"])
        return root.inputAvailable ? (root.inputMuted ? "#59616d" : "#e59b0f") : "#2b2f33"
    }

    property int preferredWidth: Math.ceil(contentRow.implicitWidth + 10)

    signal preferredWidthUpdated(int width)

    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

    function contrastTextColor(background) {
        return Contrast.contrastColor(Contrast.effectiveBackground(background, cssTheme, theme.background))
    }

    function contrastIconColor(background) {
        return Contrast.contrastColor(Contrast.effectiveBackground(background, cssTheme, theme.background))
    }

    readonly property color outputIconColor: outputCssStyle["color"]
        ? cssTheme.parseColor(outputCssStyle["color"])
        : root.contrastIconColor(root.outputBackground)
    readonly property color inputIconColor: inputCssStyle["color"]
        ? cssTheme.parseColor(inputCssStyle["color"])
        : root.contrastIconColor(root.inputBackground)

    function repaintIcons() {
        outputIcon.requestPaint()
        inputIcon.requestPaint()
    }

    onOutputBackgroundChanged: repaintIcons()
    onInputBackgroundChanged: repaintIcons()
    onOutputMutedChanged: repaintIcons()
    onInputMutedChanged: repaintIcons()
    onOutputVolumeChanged: repaintIcons()
    onInputVolumeChanged: repaintIcons()
    onOutputAvailableChanged: repaintIcons()
    onInputAvailableChanged: repaintIcons()
    onOutputCssStyleChanged: repaintIcons()
    onInputCssStyleChanged: repaintIcons()

    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    Row {
        id: contentRow
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        QBar.CssRect {
            id: outputBlock
            cssId: "pulseaudio"
            visible: root.showOutput
            cssClass: root.outputAvailable ? (root.outputMuted ? ["muted"] : []) : ["unavailable"]
            // The icon/text are colored to CONTRAST against outputBackground (yellow when
            // active), so that colour must actually be painted when the theme sets no
            // #pulseaudio background — otherwise dark-on-dark renders invisible.
            defaultColor: root.outputBackground
            width: outputRow.implicitWidth + 10
            height: parent.height

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
                        id: outputDeviceIcon
                        anchors.fill: parent
                        source: root.outputIconName
                            ? "image://themeicon/" + root.outputIconName + "|" + Contrast.toHex(root.outputIconColor).slice(1)
                            : ""
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        visible: status === Image.Ready
                    }

                    Canvas {
                        id: outputIcon
                        anchors.fill: parent
                        visible: outputDeviceIcon.status !== Image.Ready
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var outputIconColor = root.outputIconColor
                            ctx.strokeStyle = outputIconColor
                            ctx.fillStyle = outputIconColor
                            ctx.lineWidth = 1.5
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"

                            ctx.beginPath()
                            ctx.moveTo(2.6, 7.0)
                            ctx.lineTo(5.2, 7.0)
                            ctx.lineTo(7.4, 4.8)
                            ctx.lineTo(7.4, 13.2)
                            ctx.lineTo(5.2, 11.0)
                            ctx.lineTo(2.6, 11.0)
                            ctx.closePath()
                            ctx.fill()

                            if (!root.outputMuted) {
                                ctx.beginPath()
                                if (root.outputVolume < 34) {
                                    ctx.arc(8.6, 9.0, 2.0, -0.75, 0.75)
                                } else if (root.outputVolume < 67) {
                                    ctx.arc(8.6, 9.0, 2.0, -0.90, 0.90)
                                    ctx.arc(10.1, 9.0, 3.3, -0.82, 0.82)
                                } else {
                                    ctx.arc(8.6, 9.0, 2.0, -0.95, 0.95)
                                    ctx.arc(10.1, 9.0, 3.3, -0.88, 0.88)
                                    ctx.arc(11.6, 9.0, 4.4, -0.82, 0.82)
                                }
                                ctx.stroke()
                            } else {
                                ctx.beginPath()
                                ctx.moveTo(8.7, 6.5)
                                ctx.lineTo(12.0, 11.5)
                                ctx.moveTo(12.0, 6.5)
                                ctx.lineTo(8.7, 11.5)
                                ctx.stroke()
                            }
                        }

                        onVisibleChanged: if (visible) requestPaint()
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: outputCssStyle["color"]
                        ? cssTheme.parseColor(outputCssStyle["color"])
                        : root.contrastTextColor(root.outputBackground)
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

        QBar.CssRect {
            id: inputBlock
            cssId: "pulseaudio"
            visible: root.showInput
            cssClass: root.inputAvailable ? (root.inputMuted ? ["source-muted"] : ["source"]) : ["unavailable"]
            defaultColor: root.inputBackground // paint the orange mic pill (see outputBlock)
            width: inputRow.implicitWidth + 10
            height: parent.height

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
                        id: inputDeviceIcon
                        anchors.fill: parent
                        source: root.inputIconName
                            ? "image://themeicon/" + root.inputIconName + "|" + Contrast.toHex(root.inputIconColor).slice(1)
                            : ""
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        visible: status === Image.Ready
                    }

                    Canvas {
                        id: inputIcon
                        anchors.fill: parent
                        visible: inputDeviceIcon.status !== Image.Ready
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var inputIconColor = root.inputIconColor
                            ctx.strokeStyle = inputIconColor
                            ctx.fillStyle = inputIconColor
                            ctx.lineWidth = 1.5
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"

                            ctx.beginPath()
                            ctx.moveTo(8.0, 3.1)
                            ctx.bezierCurveTo(6.2, 3.1, 4.9, 4.4, 4.9, 6.1)
                            ctx.lineTo(4.9, 8.0)
                            ctx.bezierCurveTo(4.9, 9.8, 6.2, 11.1, 8.0, 11.1)
                            ctx.bezierCurveTo(9.8, 11.1, 11.1, 9.8, 11.1, 8.0)
                            ctx.lineTo(11.1, 6.1)
                            ctx.bezierCurveTo(11.1, 4.4, 9.8, 3.1, 8.0, 3.1)
                            ctx.closePath()
                            ctx.fill()

                            ctx.beginPath()
                            ctx.moveTo(8.0, 11.0)
                            ctx.lineTo(8.0, 13.3)
                            ctx.moveTo(5.8, 13.3)
                            ctx.lineTo(10.2, 13.3)
                            ctx.stroke()

                            if (root.inputMuted) {
                                ctx.beginPath()
                                ctx.moveTo(5.7, 4.9)
                                ctx.lineTo(10.2, 10.6)
                                ctx.moveTo(10.2, 4.9)
                                ctx.lineTo(5.7, 10.6)
                                ctx.stroke()
                            }
                        }

                        onVisibleChanged: if (visible) requestPaint()
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: inputCssStyle["color"]
                        ? cssTheme.parseColor(inputCssStyle["color"])
                        : root.contrastTextColor(root.inputBackground)
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
