import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch
import "Wmo.js" as Wmo

// Current weather — an EXTERNAL runtime widget (lives in ~/.config/qbar/widgets, NOT
// compiled into qbar). Hits open-meteo.com (no API key), shows <emoji> <temp>° for the
// current city; scroll cycles between configured cities, click opens a forecast popup.
//
// Enable in ~/.config/qbar/config.json:
//   "modules-right": [ "CustomTool:custom/weather" ],
//   "customTools": {
//     "custom/weather": {
//       "source": "widgets/Weather.qml",
//       "cities": ["São Paulo", "Rio de Janeiro", "Lisboa"]
//     }
//   }
// Location is by city NAME (geocoded via open-meteo). Accepts `cities` (array or
// comma-separated string) or a single `city`; or fixed `latitude`+`longitude` (+`label`).
QBar.CssRect {
    id: root

    property string toolId: ""
    // customTools[toolId] inside a binding does NOT re-evaluate when toolId changes (QML
    // doesn't track the dependency through a QVariantMap subscript), so this is assigned
    // explicitly from onToolIdChanged rather than bound.
    property var widgetConfig: ({})

    cssId: "custom-weather"
    height: theme.height
    // Sizing contract: the bar's Loader resizes the item (clearing a `width:` binding),
    // so Bar.appletWidth reads `preferredWidth` — drive that (content width), not `width`.
    property int preferredWidth: Math.max(1, content.implicitWidth + 14)
    width: Math.max(1, preferredWidth)
    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

    // Resolved cities [{ name, lat, lon }], plus which one is shown and its conditions.
    property var places: []
    property int current: 0
    readonly property var place: (current >= 0 && current < places.length) ? places[current] : null
    property real temp: NaN
    property int code: -1
    property bool isDay: true
    property bool failed: false
    readonly property var wx: code >= 0 ? Wmo.describe(code, isDay) : ({ emoji: "…", label: "" })
    property bool hovered: false

    // Resolved foreground for the bar text/emoji. The emoji is a plain Text (no cssId),
    // so without this it defaults to BLACK and is unreadable on dark/colored bars (e.g.
    // the bliss-xp blue). Resolve #custom-weather's color, else the theme foreground.
    readonly property color fg: {
        var s = root.style
        if (s && s["color"]) return cssTheme.parseColor(s["color"])
        return theme.foreground ? cssTheme.parseColor(theme.foreground) : "white"
    }

    // Which open-meteo `current` variables the popup details (N metrics). Configurable;
    // the bar itself always shows temperature.
    readonly property var metrics: root.normList(widgetConfig.metrics,
        ["temperature_2m", "apparent_temperature", "relative_humidity_2m",
         "wind_speed_10m", "uv_index", "precipitation"])

    // Normalise a config list to a JS string array. A config array arrives as a QVariantList,
    // which is NOT a JS Array (Array.isArray → false) but String()-joins with commas, so the
    // split path handles it; a real JS array or a comma string also work.
    function normList(v, fallback) {
        const list = Array.isArray(v)
            ? v.slice()
            : ((v !== undefined && v !== null) ? String(v).split(",") : [])
        const cleaned = list.map(s => String(s).trim()).filter(s => s.length > 0)
        return cleaned.length > 0 ? cleaned : fallback
    }

    function configuredCities() {
        const cfg = root.widgetConfig
        return root.normList(cfg.cities !== undefined ? cfg.cities : cfg.city, ["São Paulo"])
    }

    function init() {
        const cfg = root.widgetConfig
        // Fixed coordinates short-circuit geocoding (single location).
        if (cfg.latitude !== undefined && cfg.longitude !== undefined) {
            root.places = [{ name: cfg.label || cfg.city || "", lat: cfg.latitude, lon: cfg.longitude }]
            root.applyCurrent()
            return
        }
        Promise.all(root.configuredCities().map(name =>
            Fetch.fetch(`https://geocoding-api.open-meteo.com/v1/search?count=1&language=pt&format=json&name=${encodeURIComponent(name)}`)
                .then(r => r.json())
                .then(d => (d.results && d.results.length > 0)
                    ? { name: d.results[0].name, lat: d.results[0].latitude, lon: d.results[0].longitude }
                    : null)
                .catch(e => {
                    console.warn(`weather widget: geocode falhou (${name}):`, e.message)
                    return null
                })
        )).then(list => {
            root.places = list.filter(p => p !== null)
            root.current = 0
            if (root.places.length === 0)
                root.failed = true
            else
                root.applyCurrent()
        })
    }

    // Show the current city: reset conditions, then fetch them.
    function applyCurrent() {
        root.temp = NaN
        root.code = -1
        root.failed = false
        root.fetchWeather()
    }

    function fetchWeather() {
        if (!root.place)
            return
        const p = root.place
        Fetch.fetch(`https://api.open-meteo.com/v1/forecast?current=temperature_2m,weather_code,is_day&timezone=auto&latitude=${p.lat}&longitude=${p.lon}`)
            .then(r => r.json())
            .then(d => {
                root.temp = d.current.temperature_2m
                root.code = d.current.weather_code
                root.isDay = d.current.is_day !== 0
                root.failed = false
            })
            .catch(e => {
                root.failed = true
                console.warn("weather widget: fetch falhou:", e.message)
            })
    }

    function cycle(step) {
        if (root.places.length < 2)
            return
        root.current = (root.current + step + root.places.length) % root.places.length
        root.applyCurrent()
    }

    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // Bar.qml's Loader assigns toolId AFTER Component.onCompleted, so widgetConfig (which
    // keys off it) is empty during onCompleted — initialising there would read no `cities`
    // and fall back to the single default. Initialise once the real toolId arrives.
    property bool _inited: false
    onToolIdChanged: {
        if (toolId.length === 0)
            return
        root.widgetConfig = (typeof customTools !== "undefined" && customTools && customTools[toolId])
            ? customTools[toolId] : ({})
        if (!_inited) {
            _inited = true
            init()
        }
    }

    // Weather changes slowly — refresh the current city every 10 min (no re-geocode).
    Timer {
        interval: 600000
        running: true
        repeat: true
        onTriggered: root.fetchWeather()
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 4

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.wx.emoji
            color: root.fg
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.failed ? "—" : (!isNaN(root.temp) ? Math.round(root.temp) + "°" : "…")
            color: root.fg
            font.family: theme.fontFamily
            font.pointSize: theme.fontSize
        }
    }

    QBar.Popup {
        id: popup
        name: "weather"   // reachable via the IPC: `qbar-ipc toggle weather`
        anchorItem: root
        source: Qt.resolvedUrl("WeatherPopup.qml") // sibling external file
        payload: ({ lat: root.place ? root.place.lat : NaN,
                    lon: root.place ? root.place.lon : NaN,
                    locationName: root.place ? root.place.name : "",
                    temp: root.temp, code: root.code, isDay: root.isDay,
                    metrics: root.metrics,
                    animatedBackground: root.widgetConfig.animatedBackground !== undefined
                        ? root.widgetConfig.animatedBackground : true })
        popupWidth: 360
        popupHeight: 540
        gap: 4
        placement: "above"        // bottom bar → flip the popup upward
        horizontalAlignment: "center"
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: root.hovered && !popup.isOpen
        text: root.place
            ? `${root.place.name} · ${root.wx.label}${!isNaN(root.temp) ? " " + Math.round(root.temp) + "°" : ""}`
              + (root.places.length > 1 ? " · scroll p/ trocar cidade" : "")
            : "Clima"
        side: "auto"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onClicked: popup.toggle()
        onContainsMouseChanged: root.hovered = containsMouse
        // Scroll wheel cycles between configured cities.
        onWheel: function (wheel) {
            root.cycle(wheel.angleDelta.y < 0 ? 1 : -1)
        }
    }
}
