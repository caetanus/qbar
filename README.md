<div align="center">

# qbar

**A fast, CSS-themable status bar for Wayland and X11, built with Qt 6 / QML.**

Waybar-style modules, rich interactive popups, hot-reloadable custom QML widgets,
a JSON IPC for scripting, and a matching QML/PAM lock screen.

![qbar themes](docs/assets/themes.gif)

</div>

---

## Highlights

- **Wayland-native** via `wlr-layer-shell` (a custom Qt platform-shell integration), with an X11 dock/strut fallback.
- **Standard-CSS theming.** Themes are plain `.css` files — selectors like `#cpu`, `#workspaces button:active`, gradients, `box-shadow`, `border-radius`, transitions. 28 themes ship in [`config/themes/`](config/themes). Hot-reloads on save. The engine grew up here and now lives as its own project, [qml-css-engine](https://github.com/caetanus/qml-css-engine) (vendored as a submodule) — docs, tests and the full cascade/specificity/`@media`/`@keyframes` story live there.
- **Rich popups**, not just tooltips: an interactive CPU/Memory dashboard (per-core graphs, top processes), a calendar with events, a Bitcoin candlestick chart, disk, battery, and media.
- **Custom QML widgets** loaded from disk at runtime (no recompile) — hot-reload on save. Plus **waybar-format custom tools** (external scripts) for drop-in compatibility.
- **JSON IPC** over a `QLocalSocket` — open/toggle popups from keyboard shortcuts or scripts.
- **Try a theme before you keep it.** `qbar-ipc set-css <path-or-URL>` hot-swaps the live bar to any stylesheet — even a remote one — so you can preview a community theme straight from a URL, or a file you just downloaded (a relative path resolves from your shell's cwd). `qbar-ipc reset-css` snaps back to your configured theme — no restart, no config edits.
- **Async by design.** Network (`QNetworkAccessManager`) and JSON parsing run off the GUI thread, and the marquee scrolls on the render thread, so the bar stays smooth.
- **Native notifications** — **new**: opt-in `org.freedesktop.Notifications` daemon (replaces dunst/mako) rendering toasts with the same CSS engine: frosted-glass blur, an emboss relief shader, `@keyframes` entry **and exit** animations, hover-to-expand, and stack-tag coalescing for volume/brightness OSDs. Actions, urgency states, progress/value gauges included.
- **qbar-lock** — **new**: an optional QML/PAM lock screen that shares qbar's CSS engine. Wayland `ext-session-lock-v1` (a real session lock) or an X11 grab, with password + **fingerprint (fprintd)** + face (Howdy) racing in parallel — first to succeed unlocks.
- **Speaks your language.** `LANG`/`LC_MESSAGES` is honoured across the bar and the lock screen — translations ship for **pt_BR, pt_PT, français, Deutsch and español**, dates and weekday names follow your locale, and Qt's own component catalogs are loaded too.

## Screenshots

A bar (Tokyo Night) with workspaces, CPU/memory/network graphs, disk usage and MPRIS now-playing on the left; an audio drawer, battery, a live BTC ticker, the clock, and the system tray on the right:

![qbar bar](docs/assets/bar.png)

Popups (opened by click or over the IPC):

| System dashboard (per-core graphs, top processes) | Now playing (MPRIS) |
|---|---|
| ![system](docs/assets/popup-cpu.png) | ![media](docs/assets/popup-media.png) |
| **Calendar** (events from the local calendar) | **Bitcoin candlesticks** (scroll to zoom, hover for OHLC) |
| ![calendar](docs/assets/popup-clock.png) | ![bitcoin](docs/assets/popup-bitcoin.png) |

A few of the 28 bundled themes — light and dark, including the Windows-XP-style `bliss-xp` — see the [full gallery](docs/assets/themes.png):

![theme gallery](docs/assets/themes.png)

Preview a theme live before you commit to it — `qbar-ipc set-css` hot-swaps the running bar to any stylesheet (a file you just downloaded, or even straight from a URL); `qbar-ipc reset-css` snaps back to your configured theme:

![preview a theme with qbar-ipc](docs/assets/ipc-theme.png)

## Building

qbar uses **Meson** + **Ninja** and targets **Qt 6.5+**.

The CSS engine is vendored as a **git submodule** — clone with `--recursive`
(or run `git submodule update --init` in an existing checkout):

```bash
git clone --recursive https://github.com/caetanus/qbar.git
```

**Build tools:** Meson, Ninja, a C++20 compiler, Qt's `lrelease` (translations; `qt6-tools` on Arch), and (for the Wayland integration) `wayland-scanner` + `python3`.

**Build dependencies (required):** Qt 6 (Core, Gui, Widgets, Network, Qml, Quick, DBus, **Svg**, **WebSockets**), `sqlite3`, `xkbregistry`, `libedataserver-1.2` + `libecal-2.0` (calendar), `libpulse`. Optional: `wayland-client` + `wlroots-0.19` (Wayland layer-shell), `xcb` + `xcb-ewmh` (X11), `wireplumber-0.5` (native audio backend), `libpipewire-0.3` (privacy mic/camera indicator), `pam` (lock screen).

> Svg and WebSockets are **mandatory** even though qbar only imports them from QML — `meson.build` requires them so a build can't silently produce a bar that breaks at runtime (SVG icons failing, the Bitcoin widget not loading).

**Runtime requirements (often missing on minimal installs — every one breaks a band of applets, not the whole bar):**

| Need | Why | Arch | Debian/Ubuntu |
|---|---|---|---|
| Qt 6 QML modules: QtQuick, Controls, Templates, Effects, Shapes, Layouts, Window, WorkerScript, WebSockets | the applets/popups import them | `qt6-declarative` `qt6-websockets` | `qml6-module-qtquick*` `qml6-module-qtwebsockets` |
| **Qt SVG image plugin** | every `.svg` icon (`"Unsupported image format"` without it) | `qt6-svg` | `qt6-svg-plugins` |
| an **icon theme** with symbolic icons | the `themeicon://` provider (network/bt/battery glyphs) | `adwaita-icon-theme` (or any) | `adwaita-icon-theme` |
| **evolution-data-server** daemon | the Calendar applet's `Sources` D-Bus service (the dev libs alone are not enough) | `evolution-data-server` | `evolution-data-server` |

### Installing the dependencies

Qt 6 is split across many packages and easy to get subtly wrong (it builds, then
fails at runtime). These one-liners cover **build + runtime** in one go.

**Arch** — Qt's QML modules, SVG plugin and the WebSockets module all ship inside
the base Qt packages, so this is short:

```bash
sudo pacman -S --needed \
  meson ninja cmake \
  qt6-base qt6-declarative qt6-svg qt6-websockets \
  sqlite xkbcommon-x11 libpulse evolution-data-server \
  adwaita-icon-theme ttf-nerd-fonts-symbols ttf-roboto \
  qt6-wayland wlroots0.19          # Wayland (drop for X11-only)
# optional extras: wireplumber libpipewire pam libxcb xcb-util-wm
```

**Debian / Ubuntu** — Qt's QML modules and the SVG plugin are *separate* packages
(this is the usual i3 pitfall), so they must be named explicitly:

```bash
# build
sudo apt install \
  meson ninja-build cmake pkg-config g++ \
  qt6-base-dev qt6-base-private-dev \
  qt6-declarative-dev qt6-declarative-private-dev \
  qt6-svg-dev qt6-websockets-dev \
  libsqlite3-dev libpulse-dev libxkbcommon-dev libxkbregistry-dev \
  libedataserver1.2-dev libecal2.0-dev \
  libxcb1-dev libxcb-ewmh-dev          # X11 (or libwayland-dev libwlroots-0.19-dev for Wayland)
# runtime — the pieces apt won't pull in for you
sudo apt install \
  qt6-svg-plugins \
  qml6-module-qtquick-controls qml6-module-qtquick-templates \
  qml6-module-qtquick-effects qml6-module-qtquick-shapes \
  qml6-module-qtquick-layouts qml6-module-qtwebsockets \
  qml6-module-qtqml-workerscript \
  adwaita-icon-theme evolution-data-server \
  fonts-jetbrains-mono fonts-roboto fonts-font-awesome
```

> The exact, verified Debian setup (every package, in build order) is the
> [`docker/Dockerfile`](docker/Dockerfile) — copy from there if in doubt.

**Fedora** — like Arch, the QML modules and the SVG image plugin ship inside the
base Qt packages, so the `-devel` set pulls the runtime libs in as dependencies:

```bash
sudo dnf install \
  meson ninja-build cmake gcc-c++ \
  qt6-qtbase-devel qt6-qtbase-private-devel \
  qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtwebsockets-devel \
  sqlite-devel libxkbcommon-devel \
  evolution-data-server-devel pulseaudio-libs-devel \
  libxcb-devel xcb-util-wm-devel \
  adwaita-icon-theme jetbrains-mono-fonts-all google-roboto-fonts fontawesome-fonts \
  qt6-qtwayland-devel wlroots-devel          # Wayland (drop for X11-only)
# optional extras: wireplumber-devel pipewire-devel pam-devel
```

> A true Nerd Font (patched glyphs) isn't in Fedora's repos — grab one from
> [nerdfonts.com](https://www.nerdfonts.com/) if a theme needs the extra icons.

```bash
meson setup build
ninja -C build
sudo ninja -C build install      # optional
```

Useful options (`-D<name>=<value>`):

| Option | Default | Description |
|---|---|---|
| `wayland` | `auto` | wlroots `wlr-layer-shell` integration |
| `x11` | `auto` | X11 dock/strut integration |
| `wm_backends` | `i3,hyprland` | window-manager backends: `i3`, `hyprland`, `bspwm`, `qtile`, `ewmh` |
| `wireplumber` | `auto` | native WirePlumber audio backend (else libpulse) |
| `privacy` | `auto` | mic/camera in-use indicator (needs `libpipewire-0.3`) |
| `lockscreen` | `auto` | build `qbar-lock` (needs `pam`) |
| `qml_debug` | `false` | enable the QML debug/profiler connector (development only) |

Run it: `qbar` (top bar by default). Flags: `--position top|bottom`, `--height N`, `--no-exclusive-zone`, `--no-wayland-layer-shell`.

## Configuration

qbar reads `$XDG_CONFIG_HOME/qbar/config.json` (default `~/.config/qbar/config.json`). See [`data/config.example.json`](data/config.example.json).

```jsonc
{
  "height": 30,
  "position": "top",
  "styleSheet": "~/.config/qbar/themes/tokyo-night.css",
  "windowManager": { "backend": "auto" },

  "modules-left":   ["Workspaces", "CPU", "Memory", "Network"],
  "modules-center": ["Clock"],
  "modules-right":  ["Temperature", "Sound", "Battery", "Tray"]
}
```

Built-in modules include: `Workspaces`, `Title`, `CPU`, `Memory`, `Network`, `NetworkManager`,
`Disk`, `Temperature`, `Sound`, `Battery`, `Brightness`, `Bluetooth`, `PowerProfiles`,
`Caffeine`, `XInput`, `Media` (MPRIS), `Clock`, `Tray`, `Dock`, and `CustomTool:<id>`.
`Dock` is a simple macOS-style dock applet for the active window list; add `"Dock"` to
any `modules-*` region as an alternative to the regular taskbar.

<img src="docs/assets/simple-dock.png" alt="Simple Dock applet in qbar">

Its hover animation and focused-window marker are configurable via a `"dock"` block:

```jsonc
"dock": {
  "magnify":   "fisheye",   // "fisheye" | "parabolic" | "scale" | "none"
  "indicator": "underline"  // "underline" | "dot" | "pill" | "none"
  // "hoverHeight": 48,      // whole-dock height on hover (px)
  // "peakHeight":  72       // cursor-focused fisheye peak (px)
}
```

`magnify` picks the hover effect — a cosine **fisheye** (default), a sharper
**parabolic** peak, a uniform whole-dock **scale**, or **none** (static).
`indicator` picks the focused-window marker — an accent **underline** (default),
a round **dot**, a translucent **pill** behind the icon, or **none**.

> **Window-manager support.** **sway** (Wayland), **Hyprland** (Wayland), and
> **i3** (X11) are tested setups — workspaces, popups/tooltips, the
> keyboard-layout indicator, Caffeine, resize/submap mode, etc. are validated
> there. The other backends (`bspwm`, `ewmh`) are implemented but not yet
> hardened; expect rough edges and please report bugs.

## Theming

A theme is a single standard-CSS file pointed to by `styleSheet`. Elements are selected by
id (`#cpu`, `#clock`, `#media-label`), with parts and states (`#cpu.graph`, `#workspaces button:active`).
Supports gradients, `box-shadow` (incl. inset bevels), per-corner `border-radius`, and `transition`s.
Edit the file and the bar restyles live — no restart. Browse [`config/themes/`](config/themes) for examples.

**Fonts.** The bundled themes render glyphs/icons with a **Nerd Font** (or Font Awesome) and body text with a sans family. Install at least:

- a **Nerd Font** — e.g. `ttf-jetbrains-mono-nerd` / `ttf-nerd-fonts-symbols` (Arch), `fonts-jetbrains-mono` + the [Nerd Fonts](https://www.nerdfonts.com/) symbols pack (Debian) — themes referencing `FontAwesome`/`Symbols Nerd Font` need this or icons show as tofu;
- a clean **sans** for text — `ttf-roboto` or `noto-fonts` (Arch), `fonts-roboto` / `fonts-noto-core` (Debian). A theme whose `font-family` lists `FontAwesome` first relies on the *next* family for letters, so a real text font must be installed (otherwise the icon font, which lacks letters/digits, swallows the text).
- `powerline-fonts` if a theme uses powerline separators.

## Custom widgets & tools

Two ways to extend the bar:

- **Custom QML widgets** — a `.qml` file loaded from disk at runtime (not compiled in), hot-reloaded on save. It can `import "qrc:/qbar"` (themed `CssRect`/`CssText`), read the `theme`/`cssTheme` and the data models, do async HTTP (`Fetch.js`) and async JSON (`Json.js`), and open its own popup. The bundled [`config/widgets/`](config/widgets) has live **Bitcoin** and **Weather** widgets.

  ```jsonc
  "modules-right": ["CustomTool:custom/btc"],
  "customTools": { "custom/btc": { "source": "widgets/Bitcoin.qml" } }
  ```

- **Custom tools** — waybar-format external scripts (`exec`, `interval`, `return-type: json`, `format`, `format-icons`, Pango markup), for drop-in compatibility with existing waybar modules.

## IPC

qbar exposes a small **JSON line-protocol IPC** over a `QLocalSocket` at
`$XDG_RUNTIME_DIR/qbar.sock` (override with `$QBAR_IPC_SOCKET`). One JSON object per line in,
one per line out. This makes it easy to bind a key to a popup, or swap themes while testing.

```jsonc
{"command":"toggle","popup":"cpu"}            // -> {"ok":true}
{"command":"open","popup":"memory"}           // -> {"ok":true}
{"command":"close","popup":"clock"}           // -> {"ok":true}
{"command":"close-all"}                       // -> {"ok":true}
{"command":"set-css","path":"/.../nord.css"}  // -> {"ok":true,"bars":1}   (live theme swap)
{"command":"list"}                            // -> {"ok":true,"popups":["cpu","memory","clock","battery","btc"]}
{"command":"ping"}                            // -> {"ok":true,"pong":true}
```

qbar ships a tiny client, **`qbar-ipc`**, so you don't have to hand-roll a socket client —
bind it to a key in your compositor:

```bash
qbar-ipc toggle cpu                         # prints the JSON reply; exit 0 on {"ok":true}
qbar-ipc open memory
qbar-ipc set-css ~/.config/qbar/themes/nord.css   # live theme swap (handy when testing)
qbar-ipc list
qbar-ipc ping
```

In **sway** (or i3) config:

```
bindsym $mod+c exec qbar-ipc toggle cpu
bindsym $mod+Shift+m exec qbar-ipc toggle memory
bindsym $mod+t exec qbar-ipc toggle clock
```

Popups are registered by name; any applet's popup can be exposed by giving its `QBar.Popup` a `name`.

## Notifications

qbar ships a native **`org.freedesktop.Notifications` daemon** (Desktop Notifications spec) —
the toasts render through qbar's CSS engine instead of dunst/mako. Cards support the
standard capabilities (actions, body markup, icons/images, urgency, the `value` hint for
volume/brightness-style gauges, `replaces_id`), plus a timeout countdown bar that pauses
while hovered, **hover-to-expand** (an elided "…" body grows to full text under the pointer),
and right/middle-click to dismiss anywhere on the card.

<img src="docs/assets/notifications.png" alt="qbar notifications (macchiato-notify theme)" width="400">

**Opt-in by config** — owning the notification bus name displaces your current daemon, so
nothing happens until you say so. Stop dunst/mako (`systemctl --user mask dunst.service`)
and add:

```jsonc
"notifications": {
  "enabled": true,                         // REQUIRED — off by default
  "styleSheet": "themes/nord-notify.css",  // the toasts' OWN theme (optional)
  "corner": "top-right",                   // top/bottom × left/right
  "maxVisible": 5,
  "timeout": 6000                          // ms; critical notifications never expire
}
```

If another daemon still holds the bus name, qbar waits and grabs it the moment it frees.

**Volume/brightness OSDs work out of the box** — notifications with a *stack tag*
(`x-dunst-stack-tag`, or the `synchronous` family notify-osd used) coalesce into a single
live card per app + tag, updated in place — a held volume key is one gauge, not a stack:

```bash
notify-send -h string:synchronous:volume -h int:value:72 "Volume" "72%"
```

**Own stylesheet.** `styleSheet` gives the notifier a dedicated CSS file — separate from the
bar's theme, exactly like the lock's `*-lock.css` (hot-reloads on save too). Five bundled looks
ship in [`config/themes/`](config/themes): `macchiato-notify`, `aqua-glass-notify`,
`tokyo-night-notify`, `nord-notify` and `neon-shrine-notify`. Omit the key and the toasts
inherit the bar theme's `#notification` rules (with presentable built-in defaults).

**Theming.** Everything is standard CSS on `#notification` (with `.app`, `.summary`, `.body`,
`.icon`, `.close`, `.action`, `.progress`, `.value` parts and `:low`/`:normal`/`:critical`/
`:hover` states). Entry **and exit** animations are real CSS `@keyframes` over `opacity` and
`transform` (translate/scale):

```css
@keyframes notif-in  { 0% { opacity: 0; transform: translateX(340px) scale(0.96); }
                       70% { opacity: 1; transform: translateX(-8px); }
                       100% { transform: translateX(0px); } }
@keyframes notif-out { 0% { opacity: 1; } 100% { opacity: 0; transform: translateY(-22px) scale(0.94); } }

#notification       { animation: notif-in 320ms ease-out; emboss: 0.35; }
#notification:exit  { animation: notif-out 200ms ease-in; }
```

**Frosted glass & emboss.** Backgrounds in the bundled looks are translucent on purpose: add a
compositor blur rule on the `qbar-notifications` layer namespace (Hyprland:
`layerrule = blur, qbar-notifications` + `layerrule = ignorezero, qbar-notifications`) and the
cards become gaussian-blurred glass. `emboss: <0..1>` swaps the flat fill for a shader-drawn
rounded slab with a soft gradient bevel (tunable via `emboss-highlight`, `emboss-shadow`,
`emboss-edge`) — combined with the blur it reads as frosted glass with relief.

## Lock screen

`qbar-lock` is an optional QML/PAM lock screen (built when `pam` is present) with native
**fingerprint unlock via fprintd** — unlike swaylock, the reader works out of the box,
concurrently with the password prompt. It picks its backend automatically: **Wayland** via
`ext-session-lock-v1` (a real, secure session lock on sway/Hyprland/wlroots) or **X11** via a
keyboard+pointer grab. On X11 also set `QT_QPA_PLATFORM=xcb` (the `--backend` flag only
selects the lock backend, not the Qt platform).

Two faces, chosen with `--lock-style`:

| Panel (default) | Ring (i3lock-style) |
|---|---|
| ![qbar-lock panel](docs/assets/lock-panel.png) | ![qbar-lock ring](docs/assets/lock-ring.png) |

```bash
qbar-lock                                   # panel (default): avatar, clock, name, password box
qbar-lock --lock-style ring --theme \
    /usr/share/qbar/themes/i3lock.css       # i3lock-style: solid screen + a single unlock ring
```

Failure is **loud** on both faces: the password box shakes and holds an error-red border
(the ring flashes red), including on a rejected **fingerprint scan** — which auto-clears
as the reader re-arms. `--no-avatar` hides your photo (a monogram disc replaces it).

**Parallel unlock** — password, fingerprint and face run at once; the first to succeed wins:

- **Password** (always on) — the `qbar-lock` PAM service (`pam_unix`).
- **Fingerprint** (auto-detected) — driven over **fprintd's D-Bus API** (not `pam_fprintd`), so it
  runs concurrently and cancels cleanly. Disable with `--no-fingerprint`.
- **Face** (opt-in) — `--face-pam-service qbar-lock-face`, a separate PAM stack using
  [Howdy](https://github.com/boltgolt/howdy)'s `pam_howdy` (see the example
  `/etc/pam.d/qbar-lock-face`; commented out until you install and enroll Howdy).

The user's **avatar and real name** come from AccountsService (`org.freedesktop.Accounts`, with
`~/.face` / GECOS fallbacks), and both faces show clear **Caps Lock** (loud red warning) and
**Num Lock** indicators.

```bash
bindsym $mod+Escape exec env QT_QPA_PLATFORM=xcb qbar-lock --lock-style ring    # X11
bindsym $mod+Escape exec qbar-lock --lock-style ring                           # Wayland
```

## Documentation

Full reference docs (Sphinx) live in [`docs/`](docs/) — configuration, CSS theming, and the
custom-tools QML API. Build with `sphinx-build -b html docs docs/_build/html`.

- [Notifications](docs/notifications.rst) — the opt-in daemon, CSS hooks, animations, stack tags
- [Lock screen](docs/lock-screen.rst) — faces, parallel auth, failure feedback, options
- [Window-manager backends](docs/window-manager-backends.md)
- [Tray icon resolution](docs/tray-icon-resolution.md)

## License

See the repository for license details.
