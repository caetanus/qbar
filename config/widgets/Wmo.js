.pragma library

// WMO weather interpretation codes (open-meteo `weather_code`) → emoji + short pt-BR label.
// https://open-meteo.com/en/docs — "Weather variable documentation".
// `isDay` (open-meteo `current.is_day`, default true) swaps the clear/partly-clear icons
// for night variants so we don't show a sun (☀️) at 22h.
function describe(code, isDay) {
    var night = isDay === false
    if (code === 0) return { emoji: night ? "🌙" : "☀️", label: "Céu limpo" }
    if (code === 1) return { emoji: night ? "🌙" : "🌤️", label: "Predom. limpo" }
    if (code === 2) return { emoji: night ? "☁️" : "⛅", label: "Parc. nublado" }
    if (code === 3) return { emoji: "☁️", label: "Nublado" }
    if (code === 45 || code === 48) return { emoji: "🌫️", label: "Neblina" }
    if (code >= 51 && code <= 57) return { emoji: "🌦️", label: "Garoa" }
    if (code >= 61 && code <= 67) return { emoji: "🌧️", label: "Chuva" }
    if (code >= 71 && code <= 77) return { emoji: "🌨️", label: "Neve" }
    if (code >= 80 && code <= 82) return { emoji: "🌦️", label: "Pancadas de chuva" }
    if (code >= 85 && code <= 86) return { emoji: "🌨️", label: "Pancadas de neve" }
    if (code === 95) return { emoji: "⛈️", label: "Tempestade" }
    if (code >= 96 && code <= 99) return { emoji: "⛈️", label: "Tempestade c/ granizo" }
    return { emoji: "❓", label: "—" }
}

// open-meteo `current` variable key → pt-BR label + emoji. Covers the variables valid in
// the `current=` block; unknown keys fall back to the raw key.
const METRICS = {
    temperature_2m:       { label: "Temperatura", icon: "🌡️" },
    apparent_temperature: { label: "Sensação",    icon: "🤚" },
    relative_humidity_2m: { label: "Umidade",     icon: "💧" },
    precipitation:        { label: "Precipitação", icon: "🌧️" },
    rain:                 { label: "Chuva",       icon: "🌧️" },
    showers:              { label: "Pancadas",    icon: "🌦️" },
    snowfall:             { label: "Neve",        icon: "🌨️" },
    cloud_cover:          { label: "Nuvens",      icon: "☁️" },
    pressure_msl:         { label: "Pressão",     icon: "📊" },
    surface_pressure:     { label: "Pressão sup.", icon: "📊" },
    wind_speed_10m:       { label: "Vento",       icon: "💨" },
    wind_gusts_10m:       { label: "Rajadas",     icon: "💨" },
    wind_direction_10m:   { label: "Direção",     icon: "🧭" },
    uv_index:             { label: "Índice UV",   icon: "🔆" }
}

function metricLabel(key) {
    return METRICS[key] ?? { label: key, icon: "•" }
}
