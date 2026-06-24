#!/usr/bin/env bash
# Host orchestrator: build the Debian image, open a nested X server (Xephyr) on
# this machine, then run i3 + qbar INSIDE the container against that display.
#
# The friend runs Arch/Xorg/i3 and half the applets fail; this reproduces a clean
# i3/Xorg userland on Debian (where the Qt QML runtime is split into many
# qml6-module-* packages) so we can see exactly which applets fail to load and why.
#
#   ./run.sh           build image + launch the repro session
#   ./run.sh shell     same env, but drop to a bash shell in the container
#   ./run.sh --no-build  skip the (slow) image build, just run
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/.." && pwd)"
IMAGE=qbar-debian-repro
DISPLAY_NUM=:2
GEOM=1400x900

MODE=run
BUILD=1
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    shell)      MODE=shell ;;
  esac
done

command -v Xephyr >/dev/null || { echo "Xephyr not found on host (pkg: xorg-server-xephyr)"; exit 1; }

if [ "$BUILD" = 1 ]; then
  echo "=== building $IMAGE ==="
  docker build -t "$IMAGE" "$HERE"
fi

cleanup() { [ -n "${XEPHYR_PID:-}" ] && kill "$XEPHYR_PID" 2>/dev/null || true; }
trap cleanup EXIT

echo "=== starting Xephyr on $DISPLAY_NUM ($GEOM) ==="
Xephyr "$DISPLAY_NUM" -ac -screen "$GEOM" -title "qbar @ i3 — Debian repro" >/dev/null 2>&1 &
XEPHYR_PID=$!
sleep 1

echo "=== running container ($MODE) ==="
exec docker run --rm -it \
  -e DISPLAY="$DISPLAY_NUM" \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v "$PROJ":/src \
  "$IMAGE" "$MODE"
