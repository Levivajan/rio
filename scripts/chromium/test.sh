#!/usr/bin/env bash
# Runs Clu's Chromium test binaries (docs/chromium/PLAN.md 2.3 and 3.5): clu_unittests, clu_browsertests and, with
# --interactive, clu_interactive_ui_tests, at most 4 launcher jobs, each browser test on a fresh temp profile, then
# the orphan sweep (docs/PLAN.md 12.17): no Clu process started from the out dir outlives the run.
#
# Usage: test.sh [--static] [--build] [--interactive] [--require] [--filter PATTERN] [--out DIR] [--dry-run]
#   --static       only the repo checks, no binary runs (`npm run verify`): every *_unittest and *_browsertest source
#                  under chromium/clu is listed in its BUILD.gn, and every directory's test target reaches //clu's
#   (default)      the static checks, then the binaries that exist in out/Dev; a missing one is reported and skipped
#   --build        build the binaries first with build.sh dev (hours on a cold out dir; PLAN 3.4)
#   --require      a missing binary is an error (the merge gate)
#   --interactive  also clu_interactive_ui_tests through interactive.sh (one at a time, the tests display, an idle Mac)
#   --filter P     gtest filter for every binary (default: every Clu test)
#   --dry-run      print what would run, run nothing
# Browser tests open real windows on the Mac, so only a Mac with the checkout runs them (never `npm run verify`). Refuses while
# a build owns the checkout.
# Env: CLU_CHROMIUM_ROOT, CLU_CHROMIUM_SRC (see common.sh), CLU_SHOTS_DIR (browser test screenshots).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

JOBS=4
static=0
build=0
interactive=0
require=0
dry=0
filter=
out=out/Dev
while [[ $# -gt 0 ]]; do
  case "$1" in
    --static) static=1 ;;
    --build) build=1 ;;
    --interactive) interactive=1 ;;
    --require) require=1 ;;
    --dry-run) dry=1 ;;
    --filter) [[ $# -ge 2 && -n "$2" ]] || die "--filter needs a pattern"; filter=$2; shift ;;
    --out) [[ $# -ge 2 && -n "$2" ]] || die "--out needs a directory"; out=$2; shift ;;
    -h | --help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument '$1' (see --help)" ;;
  esac
  shift
done

# Every gtest source is in its directory's BUILD.gn, and every directory with tests is in //clu's test targets: a test
# file nobody lists compiles nowhere and passes silently.
static_checks() {
  local problems=0 file dir base gn
  while IFS= read -r file; do
    dir=$(dirname "$file")
    base=$(basename "$file")
    gn="$dir/BUILD.gn"
    while [[ ! -f "$gn" && "$dir" != "$CHROMIUM_DIR/clu" ]]; do
      dir=$(dirname "$dir")
      gn="$dir/BUILD.gn"
    done
    local rel=${file#"$dir/"}
    if ! grep -qF "\"$rel\"" "$gn"; then
      say "not in $gn: $rel"
      problems=$((problems + 1))
    fi
    local target="//clu/${dir#"$CHROMIUM_DIR/clu/"}"
    case "$base" in
      *_unittest.*) grep -qF "\"$target:unit_tests\"" "$CHROMIUM_DIR/clu/BUILD.gn" ||
        { say "$target:unit_tests is not in //clu:unit_tests"; problems=$((problems + 1)); } ;;
      *_browsertest.*) grep -qF "\"$target:browser_tests\"" "$CHROMIUM_DIR/clu/BUILD.gn" ||
        { say "$target:browser_tests is not in //clu:browser_tests"; problems=$((problems + 1)); } ;;
    esac
  done < <(find "$CHROMIUM_DIR/clu" -type f \( -name '*_unittest.cc' -o -name '*_unittest.mm' -o -name '*_browsertest.cc' -o -name '*_browsertest.mm' \) | sort)
  [[ $problems -eq 0 ]] || die "$problems test sources reach no test binary"
  say "every Clu test source is in a test target"
}

static_checks
[[ $static -eq 0 ]] || exit 0

binaries=(clu_unittests clu_browsertests)

run() {
  if [[ $dry -eq 1 ]]; then
    say "would run: $*"
  else
    "$@"
  fi
}

if [[ ! -d "$SRC" ]]; then
  [[ $require -eq 0 && $build -eq 0 ]] || die "no Chromium checkout at $SRC (run sync.sh, or set CLU_CHROMIUM_SRC)"
  say "skipped: no Chromium checkout at $SRC (the Chromium tests need the checkout)"
  exit 0
fi
OUT_DIR="$SRC/$out"

if [[ $build -eq 1 ]]; then
  build_targets=("${binaries[@]}")
  [[ $interactive -eq 0 ]] || build_targets+=(clu_interactive_ui_tests)
  run "$REPO/scripts/chromium/build.sh" dev --out "$out" "${build_targets[@]}"
fi
if [[ $dry -eq 0 ]]; then
  assert_no_build
fi

shots=${CLU_SHOTS_DIR:-$REPO/../shots/chromium-tests}
status=0
ran=0
for bin in "${binaries[@]}"; do
  exe="$OUT_DIR/$bin"
  if [[ ! -x "$exe" ]]; then
    [[ $require -eq 0 ]] || die "no $exe; build it with test.sh --build"
    say "skipped $bin: not built in $OUT_DIR (test.sh --build builds it)"
    continue
  fi
  args=("--test-launcher-jobs=$JOBS")
  [[ -z "$filter" ]] || args+=("--gtest_filter=$filter")
  if [[ $bin != clu_unittests ]]; then
    # A fresh profile per run, the window out of the way, the mock keychain (PLAN 3.2).
    profile_dir=$(mktemp -d -t clu-tests)
    args+=("--user-data-dir=$profile_dir" --use-mock-keychain
      --window-position=1760,1160 --window-size=1280,800)
  fi
  say "running $bin"
  ran=$((ran + 1))
  if ! CLU_SHOTS_DIR="$shots" run "$exe" "${args[@]}"; then
    status=1
  fi
  [[ -z "${profile_dir:-}" ]] || rm -rf "$profile_dir"
  profile_dir=
done

# The interactive tests share one keyboard focus and one pointer: interactive.sh runs them one at a time, after its
# own checks (the tests display, no input for 120 seconds; docs/chromium/interactive.md).
if [[ $interactive -eq 1 ]]; then
  if [[ ! -x "$OUT_DIR/clu_interactive_ui_tests" ]]; then
    [[ $require -eq 0 ]] || die "no $OUT_DIR/clu_interactive_ui_tests; build it with test.sh --build --interactive"
    say "skipped clu_interactive_ui_tests: not built in $OUT_DIR"
  else
    interactive_args=(--out "$out")
    [[ -z "$filter" ]] || interactive_args+=(--filter "$filter")
    [[ $dry -eq 0 ]] || interactive_args+=(--dry-run)
    ran=$((ran + 1))
    "$REPO/scripts/chromium/interactive.sh" "${interactive_args[@]}" || status=1
  fi
fi

# The orphan sweep: any process still running from the out dir (a browser test's Rio.app, a helper, a GPU process).
if [[ $dry -eq 0 && -d "$OUT_DIR" ]]; then
  # shellcheck disable=SC2009
  orphans=$(ps -axo pid=,command= 2>/dev/null | grep -F "$OUT_DIR/" | grep -v -E 'grep|test\.sh' | awk '{ print $1 }' || true)
  if [[ -n "$orphans" ]]; then
    say "stopping leftover processes: $(echo "$orphans" | tr '\n' ' ')"
    # shellcheck disable=SC2086
    kill $orphans 2>/dev/null || true
    sleep 2
    # shellcheck disable=SC2086
    kill -9 $orphans 2>/dev/null || true
    status=1
  fi
fi

# A browser test's Rio.app stays out of LaunchServices.
[[ $dry -eq 1 ]] || unregister_stray_rio
[[ $ran -gt 0 || $dry -eq 1 ]] || say "no Clu test binary in $OUT_DIR; nothing ran"
exit $status
