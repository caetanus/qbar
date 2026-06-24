#!/usr/bin/env bash
# Entry point INSIDE the Debian container.
#   build  -> configure (x11-only) + ninja into /src/build-docker
#   run    -> start i3 in the (already-running, host Xephyr) X display + launch qbar
#   shell  -> drop to bash
#   <else> -> exec args verbatim
set -euo pipefail

BUILD_DIR=/src/build-docker

do_build() {
  # Reconfigure whenever the dir isn't a *completed* meson setup. A bare `-d`
  # check is a trap: a half-configured dir (e.g. a prior failed setup) has no
  # build.ninja, so ninja would die with "loading 'build.ninja'". Keying on the
  # artifact, not the directory, makes the build self-healing.
  if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    rm -rf "$BUILD_DIR"
    meson setup "$BUILD_DIR" \
      -Dx11=enabled \
      -Dwayland=disabled \
      -Dwireplumber=disabled \
      -Dprivacy=disabled \
      -Dlockscreen=disabled \
      -Dwm_backends=i3,ewmh
  fi
  ninja -C "$BUILD_DIR"
}

start_dbus() {
  if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    eval "$(dbus-launch --sh-syntax)"
    export DBUS_SESSION_BUS_ADDRESS DBUS_SESSION_BUS_PID
  fi
}

do_run() {
  : "${DISPLAY:?DISPLAY must be set (host Xephyr) }"
  do_build

  # Software scene graph: Xephyr does not give Qt Quick a reliable GLX context.
  export QT_QUICK_BACKEND=software
  export HOME=/tmp/qbar-home
  export XDG_RUNTIME_DIR=/tmp/qbar-run
  mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
  chmod 700 "$XDG_RUNTIME_DIR"

  start_dbus

  export XDG_CONFIG_HOME=/src/docker/xdg-config
  mkdir -p "$XDG_CONFIG_HOME"

  # i3 as the window manager in the nested X server.
  i3 -c /src/docker/i3.config &
  sleep 1

  echo "=== launching qbar (x11) — watch for 'failed to load' warnings ==="
  exec "$BUILD_DIR/qbar" --config /src/docker/qbar-config.json "$@" 2>&1
}

case "${1:-run}" in
  build) shift; do_build ;;
  run)   shift || true; do_run "$@" ;;
  shell) shift; start_dbus; exec bash ;;
  *)     exec "$@" ;;
esac
