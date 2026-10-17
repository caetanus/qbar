import QtQuick
import "qrc:/qbar" as QBar

// Pending system updates — an external (runtime) widget, not compiled into qbar.
//
// Detects the distro family from /etc/os-release (ID + ID_LIKE, falling back to which
// package manager is installed) and counts the packages that have an upgrade waiting:
//   arch    checkupdates (pacman-contrib) → pamac checkupdates → pacman -Qu
//   debian  apt list --upgradable          (uses the cached index; apt update is root's job)
//   redhat  dnf check-update → yum check-update
//   gentoo  emerge -puDN @world            (slow: runs the resolver)
//
// Config:
// "custom/updates": {
//   "source": "widgets/Updates.qml",
//   "interval": 1800,          // seconds between checks
//   "aur": false,              // arch: include AUR via `pamac checkupdates -a`
//   "family": "",              // force a family (arch|debian|redhat|gentoo); "" = detect
//   "command": "",             // updater launched on left click; "" = family default
//   "check": "",               // custom shell command printing one pending package per line
//   "notifyThreshold": 100,    // notify (notify-send) when the count reaches this; 0 = never
//   "notifyEvery": 0           // also re-notify each N more packages past the threshold; 0 = once
// }
// The notification fires when the count crosses the threshold (not on every check), and
// re-arms once it drops back below. Its "Update" action opens the updater.
// Left click opens the updater (arch: `pamac-manager --updates`, re-checking once it
// closes), right click = menu with the package list, middle click re-checks.
// CSS: #custom-updates with .pending (≥1), .many (≥50), .error, .checking.
QBar.CssRect {
    id: root

    property string toolId: ""
    property var widgetConfig: ({})
    cssId: "custom-updates"
    cssClass: {
        var classes = []
        if (root.count >= 50) classes.push("pending", "many")
        else if (root.count > 0) classes.push("pending")
        if (root.error.length > 0) classes.push("error")
        if (root.checking) classes.push("checking")
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
    property string family: ""          // arch | debian | redhat | gentoo | "" (unknown)
    property string distroName: ""
    property var packages: []           // [{ name, from, to }]
    property int count: -1              // -1 = not checked yet
    property string error: ""
    property bool checking: false
    property var updated: null

    readonly property var familyIds: ({
        arch: ["arch", "manjaro", "endeavouros", "garuda", "artix", "cachyos", "archarm"],
        debian: ["debian", "ubuntu", "linuxmint", "pop", "raspbian", "kali", "elementary", "zorin", "neon"],
        redhat: ["rhel", "fedora", "centos", "rocky", "almalinux", "ol", "amzn", "nobara"],
        gentoo: ["gentoo", "funtoo", "calculate"]
    })

    readonly property var defaultCommands: ({
        arch: "pamac-manager --updates",
        debian: "update-manager",
        redhat: "gnome-software --mode=updates",
        gentoo: ""
    })

    // ---- init --------------------------------------------------------------------------
    property bool _inited: false
    onToolIdChanged: {
        if (toolId.length === 0) return
        widgetConfig = (typeof customTools !== "undefined" && customTools && customTools[toolId]) ? customTools[toolId] : ({})
        if (!_inited) { _inited = true; init() }
    }

    function init() {
        pollTimer.interval = Math.max(60, parseInt(widgetConfig.interval) || 1800) * 1000
        pollTimer.start()
        detectFamily(function() { root.refresh() })
    }

    Timer { id: pollTimer; repeat: true; onTriggered: root.refresh() }

    function sh(script, timeoutMs, cb) {
        var reply = Proc.run("sh", ["-c", script], { timeout: timeoutMs })
        reply.finished.connect(function(code, out, err) { cb(null, code, out, err) })
        reply.failed.connect(function(e) { cb(String(e), -1, "", "") })
    }

    // ---- distro detection --------------------------------------------------------------
    function familyOf(ids) {
        for (var i = 0; i < ids.length; i++)
            for (var fam in familyIds)
                if (familyIds[fam].indexOf(ids[i]) >= 0) return fam
        return ""
    }

    function detectFamily(done) {
        var forced = String(widgetConfig.family || "")
        if (forced.length > 0) { root.family = forced; root.distroName = forced; done(); return }
        // os-release first; then whichever package manager exists, for exotic derivatives.
        sh("cat /etc/os-release /usr/lib/os-release 2>/dev/null; echo '---';"
           + " for b in pacman apt dnf yum emerge; do command -v $b >/dev/null 2>&1 && echo $b; done",
           5000, function(fail, code, out) {
            var parts = (out || "").split("---")
            var fields = {}
            parts[0].split("\n").forEach(function(line) {
                var m = line.match(/^([A-Z_]+)=(.*)$/)
                if (m && fields[m[1]] === undefined) fields[m[1]] = m[2].replace(/^["']|["']$/g, "")
            })
            root.distroName = fields.PRETTY_NAME || fields.NAME || ""
            var ids = [String(fields.ID || "").toLowerCase()]
                      .concat(String(fields.ID_LIKE || "").toLowerCase().split(/\s+/))
                      .filter(function(s) { return s.length > 0 })
            var fam = familyOf(ids)
            if (!fam && parts.length > 1) {
                var bins = parts[1].split(/\s+/)
                var byBin = { pacman: "arch", apt: "debian", dnf: "redhat", yum: "redhat", emerge: "gentoo" }
                for (var i = 0; i < bins.length && !fam; i++) fam = byBin[bins[i]] || ""
            }
            root.family = fam
            done()
        })
    }

    // ---- checking ----------------------------------------------------------------------
    function checkScript() {
        if (widgetConfig.check) return String(widgetConfig.check)
        if (family === "arch") {
            if (widgetConfig.aur === true)
                return "pamac checkupdates -a -q 2>/dev/null; true"
            // checkupdates: 0 = updates, 2 = none, anything else is a real failure.
            return "if command -v checkupdates >/dev/null; then checkupdates; c=$?; [ $c -eq 2 ] && exit 0; exit $c;"
                 + " elif command -v pamac >/dev/null; then pamac checkupdates -q 2>/dev/null; true;"
                 + " else pacman -Qu; true; fi"
        }
        if (family === "debian") return "LC_ALL=C apt list --upgradable 2>/dev/null"
        if (family === "redhat")
            // check-update exits 100 when there are updates.
            return "if command -v dnf >/dev/null; then pm=dnf; else pm=yum; fi;"
                 + " LC_ALL=C $pm -q check-update; c=$?; [ $c -eq 100 ] && exit 0; exit $c"
        if (family === "gentoo") return "emerge -puDN --quiet --color=n @world 2>/dev/null; true"
        return ""
    }

    function parse(out) {
        var list = []
        var lines = out.split("\n")
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].replace(/\s+$/, "")
            if (line.length === 0) continue
            var m
            if (family === "debian") {
                // pkg/suite 1.2-3 amd64 [upgradable from: 1.2-2]
                m = line.match(/^([^\/\s]+)\/\S+\s+(\S+)\s+\S+\s+\[upgradable from: ([^\]]+)\]/)
                if (m) list.push({ name: m[1], from: m[3], to: m[2] })
            } else if (family === "redhat") {
                if (/^(Obsoleting|Security:|Last metadata)/.test(line)) { if (/^Obsoleting/.test(line)) break; continue }
                // pkg.arch  1.2-3.fc40  updates
                m = line.match(/^(\S+)\.\S+\s+(\S+)\s+\S+$/)
                if (m) list.push({ name: m[1], from: "", to: m[2] })
            } else if (family === "gentoo") {
                // [ebuild     U  ] cat/pkg-1.2 [1.1]
                m = line.match(/^\[ebuild[^\]]*\]\s+(\S+)(?:\s+\[([^\]]+)\])?/)
                if (m) list.push({ name: m[1], from: m[2] || "", to: "" })
            } else {
                // arch: "pkg 1.0-1 -> 1.1-1" (pamac pads with two spaces); custom: any line
                m = line.match(/^(\S+)\s+(\S+)\s+->\s+(\S+)/)
                if (m) list.push({ name: m[1], from: m[2], to: m[3] })
                else if (widgetConfig.check || family !== "arch") list.push({ name: line, from: "", to: "" })
            }
        }
        return list
    }

    function refresh() {
        if (checking) return
        var script = checkScript()
        if (script.length === 0) { root.error = qsTr("unsupported distro"); return }
        root.checking = true
        sh(script, family === "gentoo" ? 600000 : 180000, function(fail, code, out, err) {
            root.checking = false
            if (fail || code !== 0) {
                root.error = fail || (err.trim().split("\n").pop() || qsTr("exit code %1").arg(code))
                console.warn("updates:", error)
                return
            }
            root.error = ""
            root.packages = parse(out)
            root.count = packages.length
            root.updated = new Date()
            maybeNotify()
        })
    }

    // ---- notification -----------------------------------------------------------------
    readonly property int notifyThreshold: widgetConfig.notifyThreshold !== undefined
        ? (parseInt(widgetConfig.notifyThreshold) || 0) : 100
    readonly property int notifyEvery: Math.max(0, parseInt(widgetConfig.notifyEvery) || 0)
    property int notifiedAt: -1         // count we last notified at; -1 = armed

    function maybeNotify() {
        if (notifyThreshold <= 0) return
        if (count < notifyThreshold) { root.notifiedAt = -1; return }
        if (notifiedAt >= 0 && (notifyEvery <= 0 || count < notifiedAt + notifyEvery)) return
        root.notifiedAt = count
        var body = (distroName.length > 0 ? distroName + ": " : "")
                   + qsTr("%n package(s) waiting to be updated", "", count)
        var args = ["-a", "qbar", "-i", "system-software-update", "-u", count >= notifyThreshold * 2 ? "critical" : "normal"]
        if (updaterCommand.length > 0) args.push("-A", "open=" + qsTr("Update"), "-w")
        args.push(qsTr("Updates available"), body)
        // -w keeps notify-send alive until the bubble closes and prints the chosen action.
        var reply = Proc.run("notify-send", args, {})
        reply.finished.connect(function(code, out) { if (out.trim() === "open") root.openUpdater() })
        reply.failed.connect(function(e) { console.warn("updates: notify-send:", e) })
    }

    // ---- updater ----------------------------------------------------------------------
    readonly property string updaterCommand: widgetConfig.command !== undefined && widgetConfig.command !== ""
        ? String(widgetConfig.command) : (defaultCommands[family] || "")

    function openUpdater() {
        var cmd = updaterCommand
        if (cmd.length === 0) { refresh(); return }
        var bin = cmd.split(/\s+/)[0]
        // Detached (setsid) so it outlives a qbar restart; re-check once it exits.
        sh("command -v " + bin + " >/dev/null || exit 127; setsid -f " + cmd + " >/dev/null 2>&1",
           5000, function(fail, code) {
            if (code === 127) { root.error = qsTr("%1 not installed").arg(bin); return }
            root.watchBin = bin.replace(/.*\//, "")
            watchTimer.start()
        })
    }

    property string watchBin: ""
    Timer {
        id: watchTimer
        interval: 3000
        repeat: true
        onTriggered: root.sh("pgrep -x " + root.watchBin + " >/dev/null", 3000, function(fail, code) {
            if (code === 1) { watchTimer.stop(); root.refresh() }
        })
    }

    // ---- text -------------------------------------------------------------------------
    readonly property string displayText: {
        if (count < 0) return checking ? "…" : (error.length > 0 ? "!" : "…")
        return String(count)
    }

    function lastUpdateText() {
        if (checking) return qsTr("checking…")
        if (!updated) return qsTr("not checked yet")
        return qsTr("checked %1").arg(updated.toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
    }

    function pkgText(p) {
        if (p.from && p.to) return p.name + "  " + p.from + " → " + p.to
        if (p.to) return p.name + "  " + p.to
        if (p.from) return p.name + "  (" + p.from + ")"
        return p.name
    }

    readonly property string tooltipText: {
        var lines = []
        lines.push(distroName.length > 0 ? distroName + (family ? " (" + family + ")" : "") : qsTr("unknown distro"))
        if (count >= 0) lines.push(count === 0 ? qsTr("system is up to date") : qsTr("%n update(s) pending", "", count))
        if (error.length > 0) lines.push("⚠ " + error)
        lines.push(lastUpdateText())
        return lines.join("\n")
    }

    // ---- menu -------------------------------------------------------------------------
    readonly property int menuLimit: 25

    function buildMenu() {
        var items = []
        items.push({ text: tooltipText.split("\n")[0], enabled: false })
        items.push({ separator: true })
        if (count === 0) items.push({ text: qsTr("System is up to date"), enabled: false })
        for (var i = 0; i < packages.length && i < menuLimit; i++)
            items.push({ text: pkgText(packages[i]), enabled: false })
        if (packages.length > menuLimit) items.push({ text: qsTr("… and %1 more").arg(packages.length - menuLimit), enabled: false })
        if (error.length > 0) items.push({ text: "⚠ " + error, enabled: false })
        items.push({ separator: true })
        if (updaterCommand.length > 0) items.push({ text: qsTr("Open updater"), action: "open" })
        items.push({ text: qsTr("Check now"), action: "refresh", enabled: !checking })
        items.push({ text: lastUpdateText(), enabled: false })
        return items
    }

    QBar.MenuPopup {
        id: menu
        anchorItem: root
        gap: 4
        onTriggered: function(index, item) {
            if (item.action === "open") root.openUpdater()
            else if (item.action === "refresh") root.refresh()
        }
    }

    QBar.Tooltip {
        anchorItem: root
        hovered: mouseArea.containsMouse
        text: root.tooltipText
        side: "auto"
    }

    // ---- rendering --------------------------------------------------------------------
    readonly property color textColor: cssStyle["color"] ? cssTheme.parseColor(cssStyle["color"]) : theme.foreground

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 5

        // Icon-theme glyph; a theme `icon` / `icon-name` wins.
        QBar.CssIcon {
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            style: root.cssStyle
            fallbackIconName: root.count > 0 ? "system-software-update" : "package-x-generic"
            color: root.textColor
            opacity: root.checking ? 0.5 : 1
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

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.MiddleButton) root.refresh()
            else if (mouse.button === Qt.RightButton) { menu.model = root.buildMenu(); menu.toggle() }
            else root.openUpdater()
        }
    }
}
