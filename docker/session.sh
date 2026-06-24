#!/usr/bin/env bash
# Canonical i3 + qbar repro session — run INSIDE the container.
#
# Display backend: a ROOTFUL Xwayland (started on the host, nested in sway). Unlike
# Xephyr — whose nested GLX only ever hands clients llvmpipe — rootful Xwayland gives
# clients NATIVE hardware GL via DRI3 (Intel UHD 630, GL 4.6). So qbar runs with a
# real GPU context directly: no VirtualGL, no readback layer, no glitches.
set -u
export DISPLAY="${DISPLAY:-:3}"

# Idempotent: tear down a previous session first.
pkill -x qbar 2>/dev/null; pkill -x i3 2>/dev/null; pkill -x picom 2>/dev/null
pkill -x pasystray 2>/dev/null; pkill -x nm-applet 2>/dev/null
sleep 1

eval "$(dbus-launch --sh-syntax)"; export DBUS_SESSION_BUS_ADDRESS

# Audio — a null sink so the Sound applet shows a real level instead of "--".
pulseaudio --start --exit-idle-time=-1 >/tmp/pa.log 2>&1
sleep 1
pactl load-module module-null-sink sink_name=dummy >/dev/null 2>&1
pactl set-default-sink dummy >/dev/null 2>&1
pactl set-sink-volume dummy 55% >/dev/null 2>&1

# Window manager.
i3 -c /src/docker/i3.config >/src/docker/i3.log 2>&1 &
sleep 2

# Compositor (xrender = hardware 2D via Xephyr glamor).
picom --backend xrender --vsync >/tmp/picom.log 2>&1 &
sleep 1

# Tray clients.
pasystray >/tmp/pasystray.log 2>&1 &
nm-applet  >/tmp/nm.log 2>&1 &

# The bar — native hardware GL (Xwayland DRI3); no VirtualGL wrapper needed.
#   QML_DISABLE_DISTANCEFIELD=1 → native FreeType glyph rasterisation. The GL
#   distance-field glyph atlas drops high-frequency updating text (clock, btc
#   ticker) on this driver, leaving it blank; native rendering is stable.
export QML_DISABLE_DISTANCEFIELD=1
exec /src/build-docker/qbar --config /src/docker/qbar-config.json
