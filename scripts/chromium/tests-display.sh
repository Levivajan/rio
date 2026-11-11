#!/usr/bin/env bash
# The "Clu tests" display (process vdisplay): an invisible display beside the main screen where
# every test window opens. Start it before a test run and stop it when the run ends, so it never runs idle.
#
# Usage: tests-display.sh start | stop | status
#   start    starts vdisplay unless it runs; prints "started" (this call started it) or "running"
#   stop     stops vdisplay, but only when no window is on it: a window there would land on the main screen. It
#            names the windows and fails instead
#   status   running or stopped, and the windows on it
# Env: CLU_VDISPLAY (default ~/.clu/vdisplay). The display spans global points
# x 1708 to 3148, y 1117 to 2017 (edge/vdisplay.json).
set -euo pipefail

VDISPLAY=${CLU_VDISPLAY:-$HOME/.clu/vdisplay}
BOUNDS=(1708 1117 3148 2017)

# Windows whose center is on the display: "app pid N" lines.
windows_on_display() {
  osascript -l JavaScript - "${BOUNDS[@]}" <<'JS'
ObjC.import('CoreGraphics')
function run(argv) {
  const [x0, y0, x1, y1] = argv.map(Number)
  const list = ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly, 0))) || []
  const out = []
  for (const w of list) {
    const b = w.kCGWindowBounds
    if (!b || w.kCGWindowLayer !== 0) continue
    const cx = b.X + b.Width / 2
    const cy = b.Y + b.Height / 2
    if (cx >= x0 && cx < x1 && cy >= y0 && cy < y1) out.push(`${w.kCGWindowOwnerName} pid ${w.kCGWindowOwnerPID}`)
  }
  return out.join('\n')
}
JS
}

running() { pgrep -x vdisplay >/dev/null; }

case "${1:-}" in
  start)
    if running; then echo running; exit 0; fi
    [[ -x "$VDISPLAY" ]] || { echo "tests-display: no $VDISPLAY" >&2; exit 2; }
    nohup "$VDISPLAY" >/dev/null 2>"${TMPDIR:-/tmp}/clu-vdisplay.err" &
    for _ in $(seq 1 50); do
      if running && system_profiler SPDisplaysDataType 2>/dev/null | grep -q "Clu tests"; then echo started; exit 0; fi
      sleep 0.1
    done
    running && { echo started; exit 0; }
    echo "tests-display: vdisplay did not come up" >&2
    exit 1
    ;;
  stop)
    running || { echo stopped; exit 0; }
    left=$(windows_on_display)
    if [[ -n "$left" ]]; then
      echo "tests-display: not stopping, these windows are on it and would move to the main screen:" >&2
      echo "$left" >&2
      exit 3
    fi
    pkill -x vdisplay
    for _ in $(seq 1 30); do running || { echo stopped; exit 0; }; sleep 0.1; done
    echo "tests-display: vdisplay did not exit" >&2
    exit 1
    ;;
  status)
    if running; then echo "running (pid $(pgrep -x vdisplay | head -1))"; else echo stopped; fi
    windows_on_display
    ;;
  *)
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
