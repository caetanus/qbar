# Window Manager Backend Planning

This document plans support for Hyprland and other tiling window managers through a backend architecture similar to the current i3/sway IPC implementation.

## Current State

QBar currently exposes compositor/window-manager state through `I3IpcClient`.

The current object owns:

- workspace list and states
- active window title
- keyboard layout
- focused container id
- workspace activation commands
- relative workspace navigation
- raw command execution for i3/sway-specific integration

The QML layer consumes this object through context properties such as `i3Ipc` and `workspaceModel`.

This works for i3 and sway because sway intentionally follows the i3 IPC protocol. It does not scale cleanly to Hyprland, bspwm, Qtile, or generic EWMH/X11 window managers.

## Goal

Introduce a generic window-manager backend layer with a stable QML-facing API.

The bar should not care if the source is i3, sway, Hyprland, bspwm, Qtile, or X11 EWMH. Backend differences should be isolated in C++ or in an external helper where that is more pragmatic.

## Proposed Architecture

Create `src/wm/`.

Suggested files:

- `src/wm/windowmanagerbackend.h`
- `src/wm/windowmanagerbackend.cpp`
- `src/wm/workspacemodel.h`
- `src/wm/workspacemodel.cpp`
- `src/wm/wmbackendfactory.h`
- `src/wm/wmbackendfactory.cpp`
- `src/wm/i3swaybackend.*`
- `src/wm/hyprlandbackend.*`
- `src/wm/bspwmbackend.*`
- `src/wm/qtilebackend.*`
- `src/wm/ewmhbackend.*`
- `src/wm/nullbackend.*`

The existing `WorkspaceModel` should move out of `src/ipc/i3ipcclient.*` and become backend-independent.

## Backend Interface

Minimum interface:

```cpp
class WindowManagerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString currentWindowTitle READ currentWindowTitle NOTIFY currentWindowTitleChanged)
    Q_PROPERTY(QString currentKeyboardLayout READ currentKeyboardLayout NOTIFY currentKeyboardLayoutChanged)
    Q_PROPERTY(qint64 focusedContainerId READ focusedContainerId NOTIFY focusedContainerChanged)

public:
    explicit WindowManagerBackend(QObject *parent = nullptr);
    virtual QString name() const = 0;
    virtual WorkspaceModel *workspaceModel() = 0;
    virtual QString currentWindowTitle() const = 0;
    virtual QString currentKeyboardLayout() const = 0;
    virtual qint64 focusedContainerId() const = 0;

public slots:
    virtual void start() = 0;
    virtual void runCommand(const QString &command) = 0;
    virtual void activateWorkspace(const QString &workspaceIdOrName) = 0;
    virtual void activateRelativeWorkspace(int direction) = 0;
    virtual void cycleKeyboardLayout() = 0;

signals:
    void currentWindowTitleChanged();
    void currentKeyboardLayoutChanged();
    void focusedContainerChanged(qint64 containerId);
    void containerFocusEvent(qint64 containerId);
    void workspaceFocusEvent();
};
```

The QML context should expose:

- `wm`
- `workspaceModel`

For one transition phase, `i3Ipc` can also point to the same backend object to avoid breaking existing QML while the applets are migrated.

## Configuration

Add config:

```json
{
  "windowManager": {
    "backend": "auto"
  }
}
```

Allowed values:

- `auto`
- `i3`
- `sway`
- `hyprland`
- `bspwm`
- `qtile`
- `ewmh`
- `none`

## Auto Detection

Detection order:

1. `SWAYSOCK` exists: `I3SwayBackend`
2. `I3SOCK` exists: `I3SwayBackend`
3. `HYPRLAND_INSTANCE_SIGNATURE` exists: `HyprlandBackend`
4. `bspc --print-socket-path` succeeds: `BspwmBackend`
5. `qtile` command responds: `QtileBackend`
6. X11 session with EWMH atoms: `EwmhBackend`
7. fallback: `NullBackend`

The selected backend should be logged on startup.

## i3/Sway Backend

Refactor the current `I3IpcClient` into `I3SwayBackend`.

Keep the existing implementation mostly intact:

- i3 IPC magic header
- command socket
- event socket
- workspace/window/input subscription
- tree snapshot parsing
- sway `GET_INPUTS` support

Sway and i3 can stay in the same backend because sway implements the i3 IPC protocol for these features.

## Hyprland Backend

Hyprland should be a first-class native backend.

Hyprland exposes two UNIX sockets:

- `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock`
- `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock`

The command socket is used for request/response commands similar to `hyprctl`. The event socket streams lines in the form:

```text
EVENT>>DATA
```

Important constraint: Hyprland documents the command socket as synchronous and warns that unclosed connections can freeze Hyprland until timeout. The backend must open, write, read, and close per request.

Initial snapshots:

- `j/workspaces`
- `j/activeworkspace`
- `j/monitors`
- `j/clients`
- `j/devices`

Relevant events:

- `workspacev2`
- `focusedmonv2`
- `createworkspacev2`
- `destroyworkspacev2`
- `renameworkspace`
- `moveworkspacev2`
- `activewindowv2`
- `windowtitlev2`
- `urgent`
- `activelayout`
- `openwindow`
- `closewindow`
- `movewindowv2`

Commands:

- activate workspace: `dispatch workspace <id-or-name>`
- relative workspace: use Hyprland dispatcher semantics for next/previous workspace
- focus window/taskbar support later: `dispatch focuswindow address:<address>`

Waybar has dedicated Hyprland modules for workspaces, window, window count, language, and submap. QBar should follow the same idea: use Hyprland IPC directly instead of trying to force it through the i3/sway model.

## bspwm Backend

First implementation should use `bspc` through `QProcess`.

bspwm is controlled via `bspc`, and `bspc --print-socket-path` can detect whether bspwm is reachable.

Initial snapshots:

- `bspc query -D --names`
- `bspc query -D -d .focused --names`
- `bspc query -D -d .occupied --names`
- `bspc query -D -d .urgent --names`
- focused node title via node query/tree dump

Events:

- persistent process: `bspc subscribe`
- `desktop_focus`
- `desktop_add`
- `desktop_remove`
- `desktop_rename`
- `node_focus`
- `node_title`
- `node_flag`

Commands:

- activate workspace: `bspc desktop -f <name>`
- relative next: `bspc desktop -f next.local`
- relative previous: `bspc desktop -f prev.local`

This matches the style of Polybar's `bspwm` module.

## Qtile Backend

Qtile should start as a helper-process backend instead of linking QBar directly to Qtile internals.

Create:

- `scripts/qbar-qtile-ipc.py`

The C++ backend starts the helper and communicates through JSON-lines:

- helper stdout: events/snapshots
- helper stdin: commands

This is preferable because Qtile's API is Python-first and based on its command graph/interfaces.

Responsibilities of the Python helper:

- list groups
- list screens
- detect active group
- detect urgent windows
- detect active window title
- subscribe to Qtile hooks
- execute group navigation commands

The C++ backend remains simple:

- spawn helper
- parse JSON lines
- write command JSON
- restart helper on crash

## EWMH Backend

This is the generic X11 fallback, similar to Polybar's `xworkspaces`.

Use XCB and EWMH atoms:

- `_NET_NUMBER_OF_DESKTOPS`
- `_NET_CURRENT_DESKTOP`
- `_NET_DESKTOP_NAMES`
- `_NET_ACTIVE_WINDOW`
- `_NET_WM_NAME`
- `_NET_WM_VISIBLE_NAME`
- `_NET_WM_STATE`
- `_NET_WM_STATE_DEMANDS_ATTENTION`

Commands:

- activate workspace by sending `_NET_CURRENT_DESKTOP`

This backend should cover many non-specific X11 window managers, including EWMH-capable setups such as awesome, xmonad with EWMH, Openbox, XFWM, and similar environments.

## Null Backend

Provide a backend that always starts successfully and exposes:

- empty workspace model
- empty current title
- empty keyboard layout
- no-op commands

This keeps the bar usable when no supported WM IPC is available.

## Meson Plan

Add Meson option:

```meson
option(
  'wm_backends',
  type: 'array',
  value: ['i3', 'hyprland', 'bspwm', 'qtile', 'ewmh'],
  choices: ['i3', 'hyprland', 'bspwm', 'qtile', 'ewmh']
)
```

Backend dependencies:

- i3/sway: QtCore, QtNetwork or QLocalSocket support from QtCore, existing JSON parsing
- Hyprland: QtCore, QLocalSocket, JSON parsing
- bspwm: QtCore, QProcess
- Qtile: QtCore, QProcess
- EWMH: XCB

No qmake integration should be added.

## Migration Phases

### Phase 1: Interface Extraction

- Move `WorkspaceModel` into `src/wm/`.
- Add `WindowManagerBackend`.
- Rename/wrap `I3IpcClient` as `I3SwayBackend`.
- Keep QML behavior unchanged.
- Expose both `wm` and temporary `i3Ipc`.

### Phase 2: Factory

- Add `WmBackendFactory`.
- Add config parsing for `windowManager.backend`.
- Add startup logs showing selected backend.
- Add `NullBackend`.

### Phase 3: Hyprland

- Implement socket path detection.
- Implement request socket helper with one connection per request.
- Implement event socket reader.
- Parse workspace/client/device snapshots.
- Wire workspace activation and title updates.
- Test on real Hyprland.

### Phase 4: EWMH

- Implement generic X11 workspace/title backend.
- Keep it independent from the existing X11 bar integration code.
- Use it as fallback for unsupported X11 WMs.

### Phase 5: bspwm

- Implement `bspc` snapshots.
- Implement persistent `bspc subscribe`.
- Add command mapping.
- Add restart logic for the subscribe process.

### Phase 6: Qtile

- Add Python helper.
- Add JSON-lines protocol.
- Add C++ process backend.
- Add restart and timeout handling.

### Phase 7: Cleanup

- Remove `i3Ipc` QML alias.
- Rename applet references to `wm`.
- Consolidate command naming.
- Add docs for supported backends and feature matrix.

## Feature Matrix

| Backend | Workspaces | Current title | Urgent | Keyboard layout | Activate workspace | Relative workspace |
| --- | --- | --- | --- | --- | --- | --- |
| i3 | yes | yes | yes | no | yes | yes |
| sway | yes | yes | yes | yes | yes | yes |
| Hyprland | yes | yes | yes | yes | yes | yes |
| bspwm | yes | yes | yes | no | yes | yes |
| Qtile | yes | yes | yes | optional | yes | yes |
| EWMH | yes | yes | partial | no | yes | yes |
| Null | empty | empty | no | no | no-op | no-op |

## Testing

Add parser-level tests for each backend before requiring a live WM.

Suggested fixtures:

- `tests/fixtures/i3/workspaces.json`
- `tests/fixtures/i3/tree.json`
- `tests/fixtures/hyprland/workspaces.json`
- `tests/fixtures/hyprland/clients.json`
- `tests/fixtures/hyprland/events.txt`
- `tests/fixtures/bspwm/events.txt`
- `tests/fixtures/ewmh/desktops.json`
- `tests/fixtures/qtile/events.jsonl`

Live integration tests should be manual at first because they need real compositor/window-manager sessions.

## References

- Hyprland IPC: https://wiki.hypr.land/IPC/
- Waybar Hyprland module: https://github.com/Alexays/Waybar/wiki/Module%3A-Hyprland
- Waybar Sway module: https://github.com/Alexays/Waybar/wiki/Module%3A-Sway
- Polybar i3 module: https://github.com/polybar/polybar/wiki/Module%3A-i3
- Polybar bspwm module: https://github.com/polybar/polybar/wiki/Module%3A-bspwm
- Polybar xworkspaces module: https://github.com/polybar/polybar/wiki/Module%3A-xworkspaces
- bspwm manual: https://github.com/baskerville/bspwm/blob/master/doc/bspwm.1.asciidoc
- Qtile command architecture: https://docs.qtile.org/en/latest/manual/commands/index.html
