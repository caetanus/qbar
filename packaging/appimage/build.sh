#!/bin/bash
# Build qbar-<version>-x86_64.AppImage in a manylinux_2_34 container.
#   [JOBS=n] packaging/appimage/build.sh [out-dir]      (default: ./dist)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$(mkdir -p "${1:-$ROOT/dist}" && cd "${1:-$ROOT/dist}" && pwd)"
IMAGE=qbar-appimage-builder
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/qbar-appimage/build"
mkdir -p "$CACHE"

git -C "$ROOT" submodule update --init vendor/qml-css-engine
docker build -t "$IMAGE" "$HERE"
docker run --rm \
    --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -e JOBS="${JOBS:-}" \
    -e QBAR_VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)" \
    -v "$ROOT:/src:ro" -v "$OUT:/out" -v "$CACHE:/build" \
    "$IMAGE" bash /src/packaging/appimage/build-inside.sh
