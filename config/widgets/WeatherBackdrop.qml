import QtQuick

// Condition-driven animated backdrop for WeatherPopup — EXTERNAL, hot-reloadable.
// A hand-rolled Canvas particle field (sun rays / drifting clouds / rain / snow / storm
// with lightning), driven by a ~30fps frame timer. Clipped to the popup's rounded corners
// in-canvas (no extra effect modules). Set `active: false` to disable.
Item {
    id: root

    property int code: -1          // open-meteo weather_code
    property bool isDay: true       // open-meteo is_day — clear sky shows a moon at night
    property bool active: true
    property string accent: "#63b3ed"
    property real radius: 12       // match the popup chrome corner radius

    // sun | night | clouds | rain | snow | storm. A clear/mostly-clear sky becomes
    // a crescent moon after dark instead of a sun.
    readonly property string category: {
        if (code < 0) return "clouds"
        if (code <= 1) return isDay ? "sun" : "night"
        if (code === 2 || code === 3 || code === 45 || code === 48) return "clouds"
        if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "snow"
        if (code >= 95) return "storm"
        if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rain"
        return "clouds"
    }

    opacity: 0.55

    property var drops: []
    property var puffs: []
    property var stars: []
    property real flash: 0
    property int tick: 0
    property real _pt: 0
    property int _pc: 0

    function rnd(a, b) { return a + Math.random() * (b - a) }

    function reset() {
        const W = Math.max(1, width), H = Math.max(1, height)
        const cat = category
        const d = [], p = []
        if (cat === "rain" || cat === "storm") {
            for (let i = 0; i < 55; ++i)
                d.push({ x: rnd(0, W), y: rnd(0, H), len: rnd(8, 18), sp: rnd(7, 13) })
        } else if (cat === "snow") {
            for (let i = 0; i < 45; ++i)
                d.push({ x: rnd(0, W), y: rnd(0, H), r: rnd(1.5, 3.5), sp: rnd(1, 2.5), ph: rnd(0, 6.28) })
        }
        if (cat === "clouds" || cat === "rain" || cat === "storm") {
            for (let i = 0; i < 5; ++i)
                p.push({ x: rnd(0, W), y: rnd(8, H * 0.5), s: rnd(0.6, 1.4), sp: rnd(0.15, 0.4) })
        }
        const st = []
        if (cat === "night") {
            for (let i = 0; i < 40; ++i)
                st.push({ x: rnd(0, W), y: rnd(0, H * 0.6), r: rnd(1.0, 2.4), ph: rnd(0, 6.28), tw: rnd(0.6, 1.4) })
        }
        drops = d
        puffs = p
        stars = st
        flash = 0
    }

    function step() {
        const W = Math.max(1, width), H = Math.max(1, height)
        const cat = category
        if (cat === "rain" || cat === "storm") {
            for (const r of drops) {
                r.y += r.sp
                r.x -= r.sp * 0.3
                if (r.y > H) { r.y = -r.len; r.x = rnd(0, W) }
                if (r.x < -5) r.x = W + 5
            }
            if (cat === "storm") {
                if (flash > 0.02) flash *= 0.82
                else { flash = 0; if (Math.random() < 0.012) flash = 1 }
            }
        } else if (cat === "snow") {
            for (const f of drops) {
                f.y += f.sp
                f.x += Math.sin((tick + f.ph * 10) * 0.05) * 0.6
                if (f.y > H) { f.y = -3; f.x = rnd(0, W) }
            }
        }
        for (const pf of puffs) {
            pf.x += pf.sp
            if (pf.x > W + 90) pf.x = -90
        }
        tick++
        canvas.requestPaint()
    }

    onWidthChanged: reset()
    onHeightChanged: reset()
    onCategoryChanged: reset()
    Component.onCompleted: reset()

    Timer {
        interval: 40   // ~25fps — ambient motion doesn't need 60/30, and this is GUI-thread work
        repeat: true
        running: root.active && root.visible && root.width > 0
        onTriggered: root.step()
    }

    function clipRounded(ctx, w, h, rad) {
        ctx.beginPath()
        ctx.moveTo(rad, 0)
        ctx.arcTo(w, 0, w, h, rad)
        ctx.arcTo(w, h, 0, h, rad)
        ctx.arcTo(0, h, 0, 0, rad)
        ctx.arcTo(0, 0, w, 0, rad)
        ctx.closePath()
        ctx.clip()
    }

    function drawCloud(ctx, x, y, s, style) {
        ctx.fillStyle = style
        ctx.beginPath()
        ctx.arc(x, y, 16 * s, 0, 6.2832)
        ctx.arc(x + 18 * s, y + 4 * s, 20 * s, 0, 6.2832)
        ctx.arc(x + 42 * s, y, 15 * s, 0, 6.2832)
        ctx.arc(x + 20 * s, y - 8 * s, 16 * s, 0, 6.2832)
        ctx.fill()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        // GPU-rasterized: Canvas.Image rasterizes on the CPU/GUI thread, and at ~25fps that
        // was enough to hitch the (render-thread) MPRIS marquee while the popup is open. FBO
        // moves rasterization to the GPU. Strategy stays Immediate so the paint and step()'s
        // particle-array mutation both run on the GUI thread — a threaded strategy would race.
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            var _t0 = Date.now()
            var ctx = getContext("2d")
            ctx.reset()
            var W = width, H = height
            if (W < 2 || H < 2)
                return
            root.clipRounded(ctx, W, H, root.radius)

            var cat = root.category
            var ac = Qt.color(root.accent)
            function rgba(c, a) {
                return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
                        + Math.round(c.b * 255) + "," + a + ")"
            }

            if (cat === "sun") {
                var cx = W * 0.78, cy = H * 0.2
                var glow = ctx.createRadialGradient(cx, cy, 4, cx, cy, 120)
                glow.addColorStop(0, "rgba(255,205,90,0.55)")
                glow.addColorStop(1, "rgba(255,205,90,0.0)")
                ctx.fillStyle = glow
                ctx.fillRect(0, 0, W, H)
                ctx.strokeStyle = "rgba(255,205,90,0.45)"
                ctx.lineWidth = 3
                var ang = root.tick * 0.01
                for (var rrr = 0; rrr < 12; ++rrr) {
                    var a = ang + rrr * (6.2832 / 12)
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(a) * 26, cy + Math.sin(a) * 26)
                    ctx.lineTo(cx + Math.cos(a) * 46, cy + Math.sin(a) * 46)
                    ctx.stroke()
                }
                ctx.fillStyle = "rgba(255,205,90,0.85)"
                ctx.beginPath()
                ctx.arc(cx, cy, 20, 0, 6.2832)
                ctx.fill()
            }

            if (cat === "night") {
                var mx = W * 0.78, my = H * 0.2
                // A medium periwinkle reads on both light and dark popup backgrounds
                // (near-white stars/moon vanish on a light theme).
                var nr = 120, ng = 140, nb = 205
                function ncol(a) { return "rgba(" + nr + "," + ng + "," + nb + "," + a + ")" }

                // Soft glow around the moon.
                var mglow = ctx.createRadialGradient(mx, my, 4, mx, my, 110)
                mglow.addColorStop(0, ncol(0.28))
                mglow.addColorStop(1, ncol(0.0))
                ctx.fillStyle = mglow
                ctx.fillRect(0, 0, W, H)

                // Twinkling stars.
                for (var sti = 0; sti < root.stars.length; ++sti) {
                    var s = root.stars[sti]
                    var tw = 0.40 + 0.50 * (0.5 + 0.5 * Math.sin(root.tick * 0.05 * s.tw + s.ph))
                    ctx.fillStyle = ncol(tw.toFixed(3))
                    ctx.beginPath()
                    ctx.arc(s.x, s.y, s.r, 0, 6.2832)
                    ctx.fill()
                }

                // Crescent: a full disc with an offset disc carved out (destination-out).
                ctx.fillStyle = ncol(0.92)
                ctx.beginPath()
                ctx.arc(mx, my, 20, 0, 6.2832)
                ctx.fill()
                ctx.globalCompositeOperation = "destination-out"
                ctx.beginPath()
                ctx.arc(mx + 9, my - 6, 18, 0, 6.2832)
                ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            }

            // Clouds (behind precip).
            for (var pi = 0; pi < root.puffs.length; ++pi) {
                var pf = root.puffs[pi]
                root.drawCloud(ctx, pf.x, pf.y, pf.s, rgba(ac, 0.18))
            }

            if (cat === "rain" || cat === "storm") {
                ctx.strokeStyle = rgba(ac, 0.5)
                ctx.lineWidth = 1.5
                ctx.beginPath()
                for (var di = 0; di < root.drops.length; ++di) {
                    var d = root.drops[di]
                    ctx.moveTo(d.x, d.y)
                    ctx.lineTo(d.x - d.len * 0.3, d.y + d.len)
                }
                ctx.stroke()
                if (cat === "storm" && root.flash > 0) {
                    ctx.fillStyle = "rgba(255,255,255," + (root.flash * 0.22).toFixed(3) + ")"
                    ctx.fillRect(0, 0, W, H)
                }
            } else if (cat === "snow") {
                ctx.fillStyle = "rgba(255,255,255,0.7)"
                for (var si = 0; si < root.drops.length; ++si) {
                    var f = root.drops[si]
                    ctx.beginPath()
                    ctx.arc(f.x, f.y, f.r, 0, 6.2832)
                    ctx.fill()
                }
            }
            root._pt += Date.now() - _t0
            root._pc++
            if (root._pc % 50 === 0)
                console.log("BD onPaint avg=" + (root._pt / root._pc).toFixed(3) + "ms over " + root._pc + " frames, cat=" + cat)
        }
    }
}
