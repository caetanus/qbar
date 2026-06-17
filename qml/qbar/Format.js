.pragma library

// Humanize a byte count to a binary-prefixed string, e.g. 9019431321 → "8.4 GiB".
function humanizeBytes(bytes) {
    var value = Number(bytes)
    if (isNaN(value) || value <= 0) {
        return "0 B"
    }
    var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
    var i = 0
    while (value >= 1024 && i < units.length - 1) {
        value /= 1024
        i += 1
    }
    var text = i === 0 ? value.toFixed(0) : value.toFixed(1).replace(/\.0$/, "")
    return text + " " + units[i]
}

// Humanize a CPU clock in MHz, e.g. 3420 → "3.42 GHz", 800 → "800 MHz".
function humanizeClock(mhz) {
    var value = Number(mhz)
    if (isNaN(value) || value <= 0) {
        return ""
    }
    if (value >= 1000) {
        return (value / 1000).toFixed(2).replace(/\.?0+$/, "") + " GHz"
    }
    return Math.round(value) + " MHz"
}
