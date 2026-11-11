#!/usr/bin/env bash
# Runs clu_interactive_ui_tests (docs/chromium/interactive.md): real keyboard and mouse input from the OS, so the
# tests take the keyboard focus, make Rio the active app and move the pointer. One test at a time, in a window on the
# tests display, each run on a fresh temp profile, then the orphan sweep (docs/PLAN.md 12.17).
#
# Usage: interactive.sh [--filter PATTERN] [--repeat N] [--out DIR] [--dry-run]
#   --filter P   gtest filter (default: every CluInteractive* test)
#   --repeat N   --gtest_repeat
#   --dry-run    print the checks and the command, run nothing
# Refuses without the tests display (process vdisplay), while a build owns the checkout, and unless the Mac has had
# no input for 120 seconds. The tests' own events count as input, so right after a run of this script "no input since
# it ended" is enough.
# Env: CLU_CHROMIUM_ROOT, CLU_CHROMIUM_SRC (see common.sh), CLU_SHOTS_DIR (default ~/.clu/shots/chromium-interactive).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

filter='CluInteractive*'
repeat=1
out=out/Dev
dry=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --filter) [[ $# -ge 2 && -n "$2" ]] || die "--filter needs a pattern"; filter=$2; shift ;;
    --repeat) [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "--repeat needs a positive number"; repeat=$2; shift ;;
    --out) [[ $# -ge 2 && -n "$2" ]] || die "--out needs a directory"; out=$2; shift ;;
    --dry-run) dry=1 ;;
    -h | --help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument '$1' (see --help)" ;;
  esac
  shift
done

EXE="$SRC/$out/clu_interactive_ui_tests"
STAMP=${TMPDIR:-/tmp}/clu-interactive-last-run

[[ -x "$EXE" ]] || die "no $EXE; build it with build.sh dev clu_interactive_ui_tests"
# The tests display runs only while tests do: start it if needed, and stop it after the run if this script
# started it (tests-display.sh refuses while another app's window is on it).
display_started=0
if [[ $dry -eq 0 ]]; then
  case "$("$(dirname "${BASH_SOURCE[0]}")/tests-display.sh" start)" in
    started) display_started=1 ;;
    running) ;;
    *) die "the tests display is missing (process vdisplay); the window would open on the main screen" ;;
  esac
fi
pgrep -x vdisplay >/dev/null || [[ $dry -eq 1 ]] || die "the tests display is missing (process vdisplay); the window would open on the main screen"
# awk reads to the end: an early exit would stop ioreg with SIGPIPE, which pipefail turns into a failure.
idle=$(ioreg -c IOHIDSystem | awk '/HIDIdleTime/ && !done { print int($NF / 1000000000); done = 1 }')
[[ "$idle" =~ ^[0-9]+$ ]] || die "could not read the Mac's idle time"
if [[ $idle -lt 120 ]]; then
  # Only this script's own events since its last run: the idle time is as old as that run's end.
  since=-1
  [[ ! -f "$STAMP" ]] || since=$(($(date +%s) - $(cat "$STAMP")))
  [[ $since -ge 0 && $idle -ge $((since - 2)) ]] ||
    die "the Mac had input ${idle}s ago; these tests need it idle for 120 seconds"
fi
[[ $dry -eq 1 ]] || assert_no_build

shots=${CLU_SHOTS_DIR:-$HOME/.clu/shots/chromium-interactive}
profile_dir=$(mktemp -d -t clu-interactive)
args=(--test-launcher-jobs=1 "--gtest_filter=$filter" "--gtest_repeat=$repeat" "--user-data-dir=$profile_dir"
  --use-mock-keychain --window-position=1760,1160 --window-size=1280,800)
say "idle ${idle}s; running clu_interactive_ui_tests ${args[*]}"
status=0
if [[ $dry -eq 1 ]]; then
  say "would run: $EXE ${args[*]}"
else
  CLU_SHOTS_DIR="$shots" "$EXE" "${args[@]}" || status=$?
  date +%s >"$STAMP"
fi
rm -rf "$profile_dir"

# The orphan sweep: nothing started from the out dir outlives the run.
if [[ $dry -eq 0 ]]; then
  # shellcheck disable=SC2009
  orphans=$(ps -axo pid=,command= 2>/dev/null | grep -F "$SRC/$out/" | grep -v -E 'grep|interactive\.sh' | awk '{ print $1 }' || true)
  if [[ -n "$orphans" ]]; then
    say "stopping leftover processes: $(echo "$orphans" | tr '\n' ' ')"
    # shellcheck disable=SC2086
    kill $orphans 2>/dev/null || true
    sleep 2
    # shellcheck disable=SC2086
    kill -9 $orphans 2>/dev/null || true
    status=1
  fi
  if [[ $display_started -eq 1 ]]; then
    "$(dirname "${BASH_SOURCE[0]}")/tests-display.sh" stop || say "the tests display stays up: a window is on it"
  fi
fi
exit $status
