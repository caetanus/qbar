#!/usr/bin/env bash
# Integration test: qbar-lock must survive its outputs going away and coming back,
# and the session must stay locked the whole time (no unlock without auth).
#
# Runs a private headless sway (its own XDG_RUNTIME_DIR and sockets, nothing on the
# real session), locks it with the qbar-lock from BUILD, then:
#   A. unplugs one of two outputs, plugs a new one;
#   B. disables every output (all wl_outputs gone, as when a laptop's panel and
#      external monitor vanish together), then re-enables them.
# After each step: qbar-lock is still running, every output has a lock surface again,
# and a second locker is DENIED (ext-session-lock `finished`) — the protocol's own
# proof that the lock is still held by a live qbar-lock. (If the locker dies, sway keeps
# the session locked — red — but lets a NEW locker take the abandoned lock; so "denied"
# is the stronger check: the lock is ours, not merely not-yet-reclaimed.)
#
# Usage: tests/lock/hotplug-sway.sh [build-dir]   (default: build)
# Needs: sway (headless backend). Not part of `meson test`: it needs a compositor.
set -u

BUILD="$(realpath "${1:-build}")"
LOCK="$BUILD/qbar-lock"
PLUGIN="$BUILD/wayland-shell-integration/libqbar-session-lock.so"
[ -x "$LOCK" ] || { echo "no $LOCK"; exit 2; }
[ -f "$PLUGIN" ] || { echo "no $PLUGIN"; exit 2; }
command -v sway >/dev/null || { echo "sway not installed"; exit 2; }

RT="$(mktemp -d)"; chmod 700 "$RT"
LOG="$RT/qbar-lock.log"
PASS=0; FAIL=0
ok()   { echo "  ok   $*"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL $*"; FAIL=$((FAIL + 1)); }

cleanup() {
    [ -n "${LOCK_PID:-}" ] && kill "$LOCK_PID" 2>/dev/null
    [ -n "${SWAY_PID:-}" ] && kill "$SWAY_PID" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$RT"
}
trap cleanup EXIT

# --- private headless sway -------------------------------------------------------
printf 'output * bg #303030 solid_color\n' > "$RT/sway.conf"
env -i HOME="$HOME" PATH="$PATH" XDG_RUNTIME_DIR="$RT" \
    WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
    sway -c "$RT/sway.conf" > "$RT/sway.log" 2>&1 &
SWAY_PID=$!
for _ in $(seq 50); do
    SOCK="$(ls "$RT"/sway-ipc.*.sock 2>/dev/null | head -1)"
    WL="$(ls "$RT" | grep -E '^wayland-[0-9]+$' | head -1)"
    [ -n "$SOCK" ] && [ -n "$WL" ] && break
    sleep 0.1
done
[ -n "${SOCK:-}" ] && [ -n "${WL:-}" ] || { echo "sway did not start"; cat "$RT/sway.log"; exit 2; }
msg() { swaymsg -s "$SOCK" "$@" >/dev/null; }
outputs() { swaymsg -s "$SOCK" -t get_outputs -r | python3 -c 'import json,sys; print(" ".join(o["name"] for o in json.load(sys.stdin) if o.get("active")))'; }
msg create_output
sleep 0.5

run_lock() {
    # exec: run in the background, the subshell BECOMES qbar-lock, so $! is its pid.
    exec env -i HOME="$HOME" PATH="$PATH" XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY="$WL" \
        QT_QPA_PLATFORM=wayland QT_PLUGIN_PATH="$BUILD" \
        QT_FORCE_STDERR_LOGGING=1 LANG=C.UTF-8 \
        DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
        "$LOCK" --backend wayland --no-fingerprint "$@"
}
surfaces() { grep -c "session-lock surface created" "$LOG"; }
alive() { kill -0 "$LOCK_PID" 2>/dev/null; }
still_locked() {
    # A second locker must be refused while the session is locked.
    local probe="$RT/probe.log"
    timeout 8 bash -c "$(declare -f run_lock); RT='$RT' WL='$WL' BUILD='$BUILD' LOCK='$LOCK' run_lock" > "$probe" 2>&1
    grep -q "finished (lock denied" "$probe"
}

echo "== lock (outputs: $(outputs))"
run_lock > "$LOG" 2>&1 &
LOCK_PID=$!
for _ in $(seq 50); do grep -q "session locked" "$LOG" && break; sleep 0.1; done
grep -q "session locked" "$LOG" && ok "session locked" || { fail "never locked"; echo "--- qbar-lock:"; grep -v libEGL "$LOG" | head -30; echo "--- sway:"; tail -15 "$RT/sway.log"; exit 1; }
grep -q "$PLUGIN" "/proc/$LOCK_PID/maps" && ok "uses the plugin from $BUILD" \
    || fail "loaded a different libqbar-session-lock.so (installed one?)"
sleep 1
[ "$(surfaces)" -ge 2 ] && ok "a lock surface per output ($(surfaces))" || fail "surfaces: $(surfaces), want 2"

echo "== A: unplug HEADLESS-2, plug a new output"
before=$(surfaces)
msg output HEADLESS-2 unplug; sleep 1
alive && ok "alive after unplug (outputs: $(outputs))" || fail "qbar-lock exited on unplug"
msg create_output; sleep 1.5
alive && ok "alive after plug (outputs: $(outputs))" || fail "qbar-lock exited on plug"
[ "$(surfaces)" -gt "$before" ] && ok "new output got a lock surface" || fail "no surface for the new output"
still_locked && ok "lock still held by qbar-lock (second locker denied)" || fail "lock no longer held after A (a second locker was granted)"

echo "== B: every output disabled, then re-enabled"
msg 'output * disable'; sleep 1.5
echo "     outputs while disabled: '$(outputs)'"
alive && ok "alive with no outputs" || fail "qbar-lock exited with no outputs"
before=$(surfaces)
msg 'output * enable'; sleep 2
alive && ok "alive after outputs return (outputs: $(outputs))" || fail "qbar-lock exited on return"
n_out=$(outputs | wc -w)
[ $(( $(surfaces) - before )) -ge "$n_out" ] && ok "every returning output got a lock surface ($n_out)" \
    || fail "surfaces after return: $(( $(surfaces) - before )), outputs: $n_out"
still_locked && ok "lock still held by qbar-lock (second locker denied)" || fail "lock no longer held after B (a second locker was granted)"

echo "== $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then echo "--- qbar-lock log:"; tail -30 "$LOG"; exit 1; fi
