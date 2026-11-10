#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BIN="${1:-$ROOT/build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000}"
command -v Xvfb >/dev/null
command -v xwininfo >/dev/null
command -v xprop >/dev/null
command -v openbox >/dev/null
test -x "$BIN"

scratch="$(mktemp -d /tmp/t3k-artemis-window.XXXXXX)"
xvfb_pid=''
app_pid=''
wm_pid=''
cleanup() {
  if [ -n "$app_pid" ]; then kill "$app_pid" 2>/dev/null || true; fi
  if [ -n "$wm_pid" ]; then kill "$wm_pid" 2>/dev/null || true; fi
  if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT

Xvfb -displayfd 3 -screen 0 1560x720x24 3> "$scratch/display" > "$scratch/xvfb.log" 2>&1 &
xvfb_pid=$!
for _ in {1..50}; do
  if [ -s "$scratch/display" ]; then break; fi
  sleep 0.1
done
test -s "$scratch/display"
export DISPLAY=":$(cat "$scratch/display")"
export GDK_BACKEND=x11
export WAYLAND_DISPLAY=
start_wm() {
  openbox > "$scratch/openbox.log" 2>&1 &
  wm_pid=$!
  for _ in {1..50}; do
    if xprop -root _NET_SUPPORTING_WM_CHECK | grep -q 'window id'; then return; fi
    sleep 0.1
  done
  echo 'Window manager did not start' >&2
  exit 1
}
start_wm

# Reuse the actual settings written on exit, then also restart the WM.
for pass in first saved-settings restarted-wm; do
  if [ "$pass" = restarted-wm ]; then
    kill "$wm_pid"
    wait "$wm_pid" || true
    start_wm
  fi
XDG_CONFIG_HOME="$scratch/config" JACK_NO_START_SERVER=1 "$BIN" > "$scratch/app.log" 2>&1 &
app_pid=$!

window_ready=0
for _ in {1..80}; do
  if xwininfo -root -tree > "$scratch/windows" 2>&1 &&
     grep -Eq '"TONE3000".*1560x720\+0\+0' "$scratch/windows"; then
    kill -0 "$app_pid"
    window_id="$(sed -n 's/^[[:space:]]*\(0x[0-9a-f]*\) "TONE3000".*/\1/p' "$scratch/windows" | head -n 1)"
    if ! xprop -id "$window_id" _NET_WM_STATE | grep -q '_NET_WM_STATE_FULLSCREEN'; then
      sleep 0.1
      continue
    fi
    window_ready=1
    break
  fi
  if ! kill -0 "$app_pid" 2>/dev/null; then
    cat "$scratch/app.log" >&2
    exit 1
  fi
  sleep 0.1
done
if [ "$window_ready" -ne 1 ]; then
  cat "$scratch/windows" >&2
  cat "$scratch/app.log" >&2
  exit 1
fi

# Let the native JUCE UI paint, then activate the Artemis header button.
sleep 3
python3 - <<'PY'
import ctypes
x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
display = x11.XOpenDisplay(None)
assert display, 'cannot open the Xvfb display'
xtst.XTestFakeMotionEvent(display, -1, 30, 24, 0)
xtst.XTestFakeButtonEvent(display, 1, 1, 0)
xtst.XTestFakeButtonEvent(display, 1, 0, 0)
x11.XFlush(display)
x11.XCloseDisplay(display)
PY
for _ in {1..50}; do
  if ! kill -0 "$app_pid" 2>/dev/null; then
    wait "$app_pid"
    app_pid=''
    break
  fi
  sleep 0.1
done
if [ -n "$app_pid" ]; then
  echo 'TONE3000 did not exit after pressing Back to Launchpad' >&2
  tail -n 30 "$scratch/app.log" >&2
  exit 1
fi
test -s "$scratch/config/TONE3000/TONE3000.settings"
echo "TONE3000 $pass: WM fullscreen, full display, and clean Launchpad exit"
done
