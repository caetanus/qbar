import QtQuick
import "qrc:/qbar" as QBar
import "qrc:/qbar/Fetch.js" as Fetch

// AI subscription quotas — an external (runtime) widget, not compiled into qbar.
//
// Shows how much of the Claude Code (claude.ai Pro/Max) and Codex CLI (ChatGPT) quota
// windows is used, read with the OAuth tokens those CLIs already keep on disk. Nothing
// is stored; tokens are never refreshed here (an expired one shows as such until the CLI
// is used again).
//
// Config:
// "custom/ai-usage": {
//   "source": "widgets/AiUsage.qml",
//   "interval": 300,                       // seconds between refreshes
//   "show": "claude:five_hour",            // window kept on the bar (see menu keys)
//   "claude": true, "codex": true,         // which services to query
//   "claudeCredentials": "~/.claude/.credentials.json",
//   "codexAuth": "~/.codex/auth.json"
// }
// Bar: <service glyph> "5h 37%" + a 2px fill. Left/right click = menu (every window with
// its reset time; tick one to keep it on the bar; refresh). Wheel cycles, middle-click
// refreshes. CSS: #custom-ai-usage with .warn (≥70%), .critical (≥90%), .error.
QBar.CssRect {
    id: root

    property string toolId: ""
    property var widgetConfig: ({})
    cssId: "custom-ai-usage"
    cssClass: {
        var classes = []
        if (root.percent >= 90) classes.push("critical")
        else if (root.percent >= 70) classes.push("warn")
        if (root.hasError) classes.push("error")
        return classes
    }
    height: theme.height
    property int preferredWidth: Math.ceil(contentRow.implicitWidth + 16)
    width: Math.max(1, preferredWidth)
    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    readonly property var cssStyle: root.style

    // ---- state -------------------------------------------------------------------------
    // services: [{ id, label, icon, path, windows: [{key, name, percent, resetsAt}], error, pending, updated }]
    property var services: []
    property string selectedKey: ""
    property int revision: 0     // bumped on every state change so bindings re-evaluate

    readonly property var selected: { revision; return findWindow(selectedKey) }
    readonly property real percent: selected ? selected.window.percent : -1
    readonly property string service: selected ? selected.service.id : ""
    readonly property string displayText: {
        revision
        if (selected) return selected.window.name + " " + Math.round(selected.window.percent) + "%"
        return isLoading() ? "…" : "--"
    }
    readonly property bool hasError: {
        revision
        for (var i = 0; i < services.length; i++) if (services[i].error) return true
        return false
    }

    function isLoading() {
        for (var i = 0; i < services.length; i++) if (services[i].pending) return true
        return false
    }

    function findWindow(key) {
        for (var i = 0; i < services.length; i++) {
            var s = services[i]
            for (var j = 0; j < s.windows.length; j++)
                if (s.windows[j].key === key) return { service: s, window: s.windows[j] }
        }
        return null
    }

    function orderedKeys() {
        var keys = []
        for (var i = 0; i < services.length; i++)
            for (var j = 0; j < services[i].windows.length; j++) keys.push(services[i].windows[j].key)
        return keys
    }

    function touch() {
        revision += 1
        var keys = orderedKeys()
        if (keys.length > 0 && keys.indexOf(selectedKey) < 0) {
            var wanted = String(widgetConfig.show || "claude:five_hour")
            selectedKey = keys.indexOf(wanted) >= 0 ? wanted : keys[0]
        }
    }

    // ---- init --------------------------------------------------------------------------
    property bool _inited: false
    onToolIdChanged: {
        if (toolId.length === 0) return
        widgetConfig = (typeof customTools !== "undefined" && customTools && customTools[toolId]) ? customTools[toolId] : ({})
        if (!_inited) { _inited = true; init() }
    }

    function init() {
        var cfg = widgetConfig
        var list = []
        if (cfg.claude !== false)
            list.push({ id: "claude", label: "Claude", icon: "ai-icons/claude.svg",
                        path: String(cfg.claudeCredentials || "~/.claude/.credentials.json"),
                        windows: [], error: "", pending: false, updated: null })
        if (cfg.codex !== false)
            list.push({ id: "codex", label: "ChatGPT", icon: "ai-icons/openai.svg",
                        path: String(cfg.codexAuth || "~/.codex/auth.json"),
                        windows: [], error: "", pending: false, updated: null })
        services = list
        pollTimer.interval = Math.max(30, parseInt(cfg.interval) || 300) * 1000
        pollTimer.start()
        refresh()
    }

    Timer { id: pollTimer; repeat: true; onTriggered: root.refresh() }
    // Quick retry for services in error (not after a 429 — that only extends the ban).
    Timer { id: retryTimer; interval: 60000; onTriggered: root.refresh(true) }

    function refresh(onlyErrored) {
        for (var i = 0; i < services.length; i++) {
            var s = services[i]
            if (s.pending) continue
            if (onlyErrored && !s.error) continue
            s.pending = true
            if (s.id === "claude") fetchClaude(s); else fetchCodex(s)
        }
        touch()
    }

    // ---- credentials (QML has no file API; the Proc global runs `cat`) -----------------
    function readJson(path, cb) {
        var expanded = path.replace(/^~(?=\/|$)/, "$HOME")
        var reply = Proc.run("sh", ["-c", "cat \"" + expanded + "\""], { timeout: 5000 })
        reply.finished.connect(function(code, out, err) {
            if (code !== 0) { cb(qsTr("not logged in (no %1)").arg(path.replace(/.*\//, "")), null); return }
            try { cb(null, JSON.parse(out)) } catch (e) { cb(qsTr("%1: %2").arg(path.replace(/.*\//, "")).arg(e.message), null) }
        })
        reply.failed.connect(function(e) { cb(String(e), null) })
    }

    function finish(s, error, windows, quickRetry) {
        s.pending = false
        s.error = error || ""
        if (!error) { s.windows = windows; s.updated = new Date() }
        else {
            console.warn("ai-usage:", s.id, error)
            if (quickRetry !== false) retryTimer.restart()
        }
        touch()
    }

    function httpError(r) {
        if (r.status === 401 || r.status === 403) return qsTr("token rejected (HTTP %1) — sign in again").arg(r.status)
        return "HTTP " + r.status
    }

    function parseResetsAt(v) {
        if (typeof v === "number") return new Date((v > 1e12 ? v : v * 1000))
        if (typeof v === "string" && v.length > 0) { var d = new Date(v); return isNaN(d.getTime()) ? null : d }
        return null
    }

    // ---- Claude: api.anthropic.com/api/oauth/usage --------------------------------------
    function claudeWindowName(key) {
        return ({ five_hour: qsTr("5h"), seven_day: qsTr("week"), seven_day_opus: qsTr("Opus week"),
                  seven_day_sonnet: qsTr("Sonnet week"), seven_day_oauth_apps: qsTr("apps week") })[key]
               || key.replace(/_/g, " ")
    }

    function fetchClaude(s) {
        readJson(s.path, function(err, root_) {
            if (err) { finish(s, err, []); return }
            var oauth = root_.claudeAiOauth || {}
            if (!oauth.accessToken) { finish(s, qsTr("no OAuth token — run `claude` and log in"), []); return }
            if (oauth.expiresAt && Date.now() > oauth.expiresAt) { finish(s, qsTr("token expired — use `claude` to refresh it"), []); return }
            Fetch.fetch("https://api.anthropic.com/api/oauth/usage", {
                headers: { "Authorization": "Bearer " + oauth.accessToken, "anthropic-beta": "oauth-2025-04-20",
                           "Accept": "application/json", "User-Agent": "qbar-ai-usage" },
                timeout: 20000
            }).then(function(r) {
                if (!r.ok) { finish(s, httpError(r), [], r.status !== 429); return null }
                return r.json()
            }).then(function(d) {
                if (!d) return
                var windows = []
                // Every object-valued key carrying `utilization` is a window; null = no such window on the plan.
                for (var k in d) {
                    var w = d[k]
                    if (!w || typeof w !== "object" || w.utilization === undefined) continue
                    windows.push({ key: "claude:" + k, name: claudeWindowName(k), percent: Number(w.utilization) || 0,
                                   resetsAt: parseResetsAt(w.resets_at) })
                }
                var rank = function(key) { return key.endsWith(":five_hour") ? 0 : (key.endsWith(":seven_day") ? 1 : 2) }
                windows.sort(function(a, b) { return rank(a.key) - rank(b.key) })
                if (windows.length === 0) finish(s, qsTr("unexpected reply (no usage windows)"), [])
                else finish(s, null, windows)
            }).catch(function(e) { finish(s, String(e.message || e), []) })
        })
    }

    // ---- Codex: chatgpt.com backend (path moved between releases; try in order) --------
    readonly property var codexUrls: [
        "https://chatgpt.com/backend-api/wham/usage",
        "https://chatgpt.com/backend-api/api/codex/usage",
        "https://chatgpt.com/backend-api/codex/usage"
    ]

    function minutesName(mins) {
        if (mins >= 6 * 24 * 60) return qsTr("week")
        if (mins >= 24 * 60) return Math.floor(mins / 1440) + "d"
        if (mins % 60 === 0) return (mins / 60) + "h"
        return mins + "m"
    }

    function fetchCodex(s) {
        readJson(s.path, function(err, root_) {
            if (err) { finish(s, err, []); return }
            var tokens = root_.tokens || {}
            if (!tokens.access_token) { finish(s, qsTr("no ChatGPT login — run `codex login`"), []); return }
            var headers = { "Authorization": "Bearer " + tokens.access_token, "Accept": "application/json", "User-Agent": "codex_cli_rs" }
            if (tokens.account_id) headers["ChatGPT-Account-Id"] = tokens.account_id
            var attempt = function(i) {
                if (i >= codexUrls.length) { finish(s, qsTr("usage endpoint not found (Codex API changed?)"), []); return }
                Fetch.fetch(codexUrls[i], { headers: headers, timeout: 20000 }).then(function(r) {
                    if (r.status === 404 || r.status === 405) { attempt(i + 1); return null }
                    if (!r.ok) { finish(s, httpError(r), [], r.status !== 429); return null }
                    return r.json()
                }).then(function(d) {
                    if (!d) return
                    var limits = d.rate_limit || d.rate_limits || d
                    var windows = []
                    var slots = { primary: "primary", primary_window: "primary", secondary: "secondary", secondary_window: "secondary" }
                    for (var slot in slots) {
                        var w = limits[slot]
                        if (!w || typeof w !== "object" || w.used_percent === undefined) continue
                        var mins = Number(w.window_duration_mins) || Math.floor((Number(w.limit_window_seconds) || 0) / 60)
                        var resetsAt = w.resets_at !== undefined ? parseResetsAt(w.resets_at)
                                     : (w.reset_after_seconds !== undefined ? new Date(Date.now() + Number(w.reset_after_seconds) * 1000) : null)
                        windows.push({ key: "codex:" + slots[slot], name: mins > 0 ? minutesName(mins) : slots[slot],
                                       percent: Number(w.used_percent) || 0, resetsAt: resetsAt })
                    }
                    if (windows.length === 0) finish(s, qsTr("unexpected reply (no rate-limit windows)"), [])
                    else finish(s, null, windows)
                }).catch(function(e) { finish(s, String(e.message || e), []) })
            }
            attempt(0)
        })
    }

    // ---- text ----------------------------------------------------------------------------
    function resetText(at) {
        if (!at) return ""
        var secs = Math.round((at.getTime() - Date.now()) / 1000)
        var clock = at.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (secs <= 0) return qsTr("resets now")
        var mins = Math.ceil(secs / 60), rel
        if (mins >= 48 * 60) rel = Math.floor(mins / 1440) + "d " + Math.floor((mins % 1440) / 60) + "h"
        else if (mins >= 60) rel = Math.floor(mins / 60) + "h " + (mins % 60) + "m"
        else rel = mins + "m"
        var sameDay = at.toDateString() === new Date().toDateString()
        return sameDay ? qsTr("resets in %1 (%2)").arg(rel).arg(clock)
                       : qsTr("resets in %1 (%2 %3)").arg(rel).arg(at.toLocaleDateString(Qt.locale(), "ddd")).arg(clock)
    }

    function lastUpdateText() {
        var latest = null
        for (var i = 0; i < services.length; i++)
            if (services[i].updated && (!latest || services[i].updated > latest)) latest = services[i].updated
        if (!latest) return isLoading() ? qsTr("fetching…") : qsTr("no data yet")
        return qsTr("updated %1").arg(latest.toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
    }

    readonly property string tooltipText: {
        revision
        var lines = []
        for (var i = 0; i < services.length; i++) {
            var s = services[i]
            if (s.windows.length === 0) { lines.push(s.label + ": " + (s.error || qsTr("fetching…"))); continue }
            var parts = s.windows.map(function(w) { return w.name + " " + Math.round(w.percent) + "%" })
            lines.push(s.label + ": " + parts.join(" · "))
            if (s.error) lines.push("  ⚠ " + s.error)
        }
        if (selected && selected.window.resetsAt) lines.push(resetText(selected.window.resetsAt))
        lines.push(lastUpdateText())
        return lines.join("\n")
    }

    // ---- menu ----------------------------------------------------------------------------
    function buildMenu() {
        var items = []
        for (var i = 0; i < services.length; i++) {
            var s = services[i]
            if (i > 0) items.push({ separator: true })
            items.push({ text: s.label, enabled: false })
            if (s.windows.length === 0) { items.push({ text: "  ⚠ " + (s.error || qsTr("fetching…")), enabled: false }); continue }
            for (var j = 0; j < s.windows.length; j++) {
                var w = s.windows[j]
                var text = "  " + w.name + "  " + Math.round(w.percent) + "%"
                var reset = resetText(w.resetsAt)
                if (reset.length > 0) text += "  · " + reset
                items.push({ text: text, checkable: true, checked: w.key === selectedKey, action: "select", key: w.key })
            }
            if (s.error) items.push({ text: "  ⚠ " + s.error, enabled: false })
        }
        items.push({ separator: true })
        items.push({ text: qsTr("Refresh now"), action: "refresh" })
        items.push({ text: lastUpdateText(), enabled: false })
        return items
    }

    QBar.MenuPopup {
        id: menu
        anchorItem: root
        gap: 4
        onTriggered: function(index, item) {
            if (item.action === "select") { root.selectedKey = item.key; root.touch() }
            else if (item.action === "refresh") root.refresh()
        }
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.tooltipText
        side: "auto"
    }

    // ---- rendering -----------------------------------------------------------------------
    readonly property color textColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground
    readonly property color fillColor: root.percent >= 90 ? "#e64553" : (root.percent >= 70 ? "#df8e1d" : theme.accent)

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 5

        // Service glyph (Claude / OpenAI, VS Code codicons); a theme `icon` wins.
        QBar.CssIcon {
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            visible: root.selected !== null || hasCustomIcon
            style: root.cssStyle
            fallbackSource: root.selected ? Qt.resolvedUrl(root.selected.service.icon) : ""
            color: root.textColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: root.textColor
            font.family: cssStyle["font-family"] || theme.fontFamily
            font.pointSize: theme.fontSize
            font.bold: true
            text: root.displayText
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        height: 2
        radius: 1
        color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.18)
        visible: root.percent >= 0
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * Math.max(0, Math.min(1, root.percent / 100))
            radius: 1
            color: root.fillColor
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.MiddleButton) root.refresh()
            else { menu.model = root.buildMenu(); menu.toggle() }
        }
        onWheel: function(wheel) {
            var keys = root.orderedKeys()
            if (keys.length < 2) return
            var idx = keys.indexOf(root.selectedKey)
            var step = wheel.angleDelta.y > 0 ? -1 : 1
            root.selectedKey = keys[((idx < 0 ? 0 : idx + step) % keys.length + keys.length) % keys.length]
            root.touch()
        }
    }
}
