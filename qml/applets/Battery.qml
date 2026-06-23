import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Contrast.js" as Contrast

QBar.CssRect {
    id: root
    cssId: "battery"
    // State → cssClass: the engine renders #battery / .charging / .full / .critical.
    cssClass: charging ? ["charging"] : full ? ["full"] : lowBattery ? ["critical"] : []
    defaultColor: "#2b2f33"
    width: 58
    height: theme.height

    readonly property var cssStyle: root.style

    property int level: batteryModel ? batteryModel.capacity : 0
    property bool charging: batteryModel ? batteryModel.charging : false
    property bool full: batteryModel ? batteryModel.full : false
    property bool lowBattery: batteryModel ? (!charging && !full && level <= 20) : false

    signal activated()

    // The painted base background (themed #battery[.state] background, else the dark
    // defaultColor). The content color contrasts against THIS, so it stays readable
    // whether the badge is dark (default) or a light themed "full" badge — fixes the
    // black-on-dark text when full and the theme sets no #battery.full background.
    readonly property color badgeBackground: cssStyle["background-color"]
        ? cssTheme.parseColor(cssStyle["background-color"]) : root.defaultColor
    readonly property color contentColor: cssStyle["color"]
        ? cssTheme.parseColor(cssStyle["color"])
        : Contrast.contrastColor(Contrast.effectiveBackground(badgeBackground, cssTheme, theme.background))

    // Critical pulse via standard CSS `@keyframes` on `#battery.critical::before` — a red
    // overlay whose opacity pulses. Replaces the old QML alertPulse colour-mix.
    readonly property var pulseStyle: cssTheme && cssTheme.loaded
        ? cssTheme.resolve("battery", ["critical"], "before") : ({})
    readonly property var pulseAnim: cssTheme && cssTheme.loaded
        ? cssTheme.parseAnimation(pulseStyle["animation"] || "") : ({})
    readonly property var pulseFrames: cssTheme && cssTheme.loaded && pulseAnim.name
        ? cssTheme.keyframes(pulseAnim.name) : []
    readonly property bool pulseEnabled: lowBattery && pulseFrames.length > 0

    QBar.Popup {
        id: batteryPopup
        name: "battery"
        anchorItem: root
        source: "qrc:/popups/BatteryPopup.qml"
        payload: ({ battery: batteryModel })
        width: 280
        height: 0
        gap: 2
        placement: "below"
        horizontalAlignment: "left"
    }

    // Base background painted by the CssRect base (per-state via cssClass).

    // Critical pulse: a red overlay whose opacity is driven through the theme's
    // @keyframes; above the base fill, below the icon/text.
    Rectangle {
        id: pulseOverlay
        anchors.fill: parent
        color: {
            var bg = root.pulseStyle["background"] || root.pulseStyle["background-color"]
            return bg ? cssTheme.parseColor(bg) : "#e62929"
        }
        opacity: 0.0
        visible: root.pulseEnabled

        QBar.CssKeyframes {
            target: pulseOverlay
            animatedProperty: "opacity"
            frames: root.pulseFrames
            duration: root.pulseAnim.duration > 0 ? root.pulseAnim.duration : 1800
            easingType: root.pulseAnim.easing !== undefined ? root.pulseAnim.easing : Easing.InOutSine
            iterations: root.pulseAnim.iterations !== undefined ? root.pulseAnim.iterations : -1
            running: root.pulseEnabled
        }
    }

    QBar.BatteryIcon {
        id: batteryIcon
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 4
        level: root.level
        charging: root.charging
        color: root.contentColor
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: batteryIcon.left
        anchors.rightMargin: 3
        color: root.contentColor
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize
        text: batteryModel ? level + "%" : "--"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: batteryPopup.toggle()
    }
}
