import QtQuick
import "qrc:/qbar" as QBar

// A waybar `group/<name>` container. Renders its child modules in a Row, styled by the
// `#<name>` CSS rule (background / border-radius / padding via the CssRect base). When the
// group declares a `drawer`, it acts as a hover-expand carousel: only the first child shows
// at rest and the rest slide out on hover (waybar's drawer idiom).
//
// Children are real applets: the URL is resolved by Bar.qml's `appletUrl` (passed in as
// `urlResolver`) so custom tools / runtime widgets work inside a group exactly as on the
// bar, and the usual signals (preferredWidthUpdated, activated, workspace*) are forwarded
// to the same context-property singletons (barWindow, i3Ipc) the bar delegate uses.
QBar.CssRect {
    id: root

    // "group/<name>" reference token (the bar slot's appletName) and its parsed def
    // ({ modules, orientation?, drawer?, id? }), assigned by Bar.qml after load.
    property string groupName: ""
    property var groupDef: ({})
    // Bar.appletUrl — reused so child applets resolve identically to top-level ones.
    property var urlResolver: null

    cssId: groupName.indexOf("group/") === 0 ? groupName.substring(6) : groupName

    readonly property var modules: (groupDef && groupDef.modules) ? groupDef.modules : []
    readonly property var drawer: (groupDef && groupDef.drawer && typeof groupDef.drawer === "object")
        ? groupDef.drawer : null
    readonly property bool hasDrawer: drawer !== null

    // The drawer's expand/collapse animation is CSS-driven, full stop: `#<name> { transition }`
    // is parsed by the CssRect base into `transitionMs` / `transitionEasingType`. The CSS is
    // the single source of truth — the waybar `drawer.transition-duration` from config is
    // translated to `#<name> { transition: <ms>ms }` and prepended to the theme (see
    // BarWindow::loadCssTheme), so it shows up here as `transitionMs` like any other rule.
    readonly property int drawerMs: transitionMs > 0 ? transitionMs
        : (theme.animationDuration > 0 ? theme.animationDuration : 200)

    // waybar `children-class` (default "drawer-child") — applied as a CSS class on each child
    // applet, so `.<class>` / `#<module>.<class>` rules can style the grouped modules.
    readonly property string childrenClass: (drawer && drawer["children-class"] !== undefined
        && String(drawer["children-class"]).length > 0)
        ? String(drawer["children-class"]) : "drawer-child"

    // Padding inside the container (CSS `padding`, first value → all sides for simplicity).
    readonly property real pad: (style && style["padding"])
        ? cssTheme.parseLength(String(style["padding"]).trim().split(/\s+/)[0], 0) : 0
    readonly property real innerSpacing: (style && style["spacing"])
        ? cssTheme.parseLength(style["spacing"], 0) : 0

    height: theme.height
    // The group sizes itself from its content; the bar slot reads preferredWidth.
    property int preferredWidth: Math.ceil(contentRow.width + pad * 2)
    width: preferredWidth

    signal preferredWidthUpdated(int width)
    onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
    Component.onCompleted: preferredWidthUpdated(preferredWidth)

    // groupDef/groupName are assigned by Bar.wireGroup() AFTER Component.onCompleted, so the
    // IPC registration waits for the def to arrive. registerPopup is idempotent (it just
    // overwrites the same name→target slot), so re-running on changes is safe.
    onGroupDefChanged: root.registerDrawerIpc()
    function registerDrawerIpc() {
        // Compute "is a drawer" straight from groupDef rather than via the `hasDrawer`
        // binding: inside this onGroupDefChanged handler the derived binding can still hold
        // its stale (pre-change) value, so reading it here would mis-skip the registration.
        var isDrawer = groupDef && groupDef.drawer && typeof groupDef.drawer === "object"
        if (isDrawer && cssId.length > 0 && typeof qbarIpc !== "undefined" && qbarIpc)
            qbarIpc.registerPopup(cssId, root)
    }

    function childWidth(item) {
        if (!item)
            return 0
        if (typeof item.preferredWidth === "number")
            return item.preferredWidth
        if (typeof item.implicitWidth === "number" && item.implicitWidth > 0)
            return item.implicitWidth
        if (typeof item.width === "number" && item.width > 0)
            return item.width
        return 0
    }

    function childToolId(name) {
        return name.indexOf("CustomTool:") === 0 ? name.substring("CustomTool:".length) : ""
    }

    // "/variant" suffix of a child module name ("Sound/out" -> "out"), mirroring Bar.qml.
    function childVariant(name) {
        if (name.indexOf("CustomTool:") === 0)
            return ""
        var slash = name.indexOf("/")
        return slash >= 0 ? name.substring(slash + 1) : ""
    }

    // Add the drawer's `children-class` to a child applet's cssClass. The applet already
    // registered with the engine (in its own Component.onCompleted, before this Loader's
    // onLoaded), so assigning cssClass fires its NOTIFY → the engine re-resolves and pushes
    // the class-qualified rules. Only applies to applets whose root carries cssClass
    // (the CssRect-based ones, i.e. most of them).
    function applyChildrenClass(item) {
        if (!hasDrawer || !item || item.cssClass === undefined)
            return
        var cls = (item.cssClass && item.cssClass.length) ? item.cssClass.slice() : []
        if (cls.indexOf(root.childrenClass) < 0) {
            cls.push(root.childrenClass)
            item.cssClass = cls
        }
    }

    // Hover anywhere over the group keeps the drawer open; without a drawer everything is
    // always visible.
    HoverHandler { id: hover }
    // A drawer expands on hover OR when opened over IPC (so a keybind can pin it open);
    // a plain group is always "expanded" (all children visible).
    property bool ipcOpen: false
    readonly property bool expanded: !hasDrawer || hover.hovered || ipcOpen

    // IPC surface: a drawer registers under its name and responds to open/toggle/close,
    // exactly like a QBar.Popup — so `qbar-ipc toggle <name>` pins/unpins it open.
    function open() { root.ipcOpen = true }
    function close() { root.ipcOpen = false }
    function toggle() { root.ipcOpen = !root.ipcOpen }

    Row {
        id: contentRow
        x: root.pad
        height: parent.height
        spacing: root.innerSpacing

        Repeater {
            model: root.modules

            delegate: Item {
                id: cslot
                property string childName: modelData
                property int childPreferred: 0
                // In a drawer, every child after the first collapses to zero width until hover.
                readonly property bool drawerHidden: root.hasDrawer && index > 0
                width: (drawerHidden && !root.expanded) ? 0 : cslot.childPreferred
                height: parent.height
                clip: true

                Behavior on width {
                    enabled: root.hasDrawer
                    NumberAnimation {
                        duration: root.drawerMs
                        easing.type: root.transitionEasingType
                    }
                }

                Loader {
                    id: cloader
                    // Left-anchored so the applet stays put while the slot width animates.
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: cslot.childPreferred > 0 ? cslot.childPreferred : root.height
                    source: root.urlResolver ? root.urlResolver(cslot.childName) : ""
                    asynchronous: false

                    onLoaded: {
                        if (cslot.childName.indexOf("CustomTool:") === 0
                            && cloader.item && "toolId" in cloader.item) {
                            cloader.item.toolId = root.childToolId(cslot.childName)
                        }
                        if (cloader.item && "variant" in cloader.item)
                            cloader.item.variant = root.childVariant(cslot.childName)
                        root.applyChildrenClass(cloader.item)
                        cslot.childPreferred = root.childWidth(cloader.item)
                    }
                }

                Connections {
                    target: cloader.item
                    ignoreUnknownSignals: true

                    function onPreferredWidthUpdated(width) {
                        cslot.childPreferred = width
                    }
                    function onActivated() {
                        if (cslot.childName === "Clock" && barWindow)
                            barWindow.openCalendar(cloader.item)
                        else if (cslot.childName === "XInput" && barWindow)
                            barWindow.cycleKeyboardLayout()
                        else if (cslot.childName === "Caffeine" && barWindow)
                            barWindow.toggleCaffeine()
                    }
                    function onWorkspaceActivated(workspaceName) {
                        if (i3Ipc)
                            i3Ipc.activateWorkspace(workspaceName)
                    }
                    function onWorkspaceScrolled(direction) {
                        if (i3Ipc)
                            i3Ipc.activateRelativeWorkspace(direction)
                    }
                }
            }
        }
    }
}
