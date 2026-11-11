#!/usr/bin/env bash
# Syncs the Chromium checkout to the tag in chromium/VERSION (docs/chromium/PLAN.md 2.3): shallow, no history,
# 4 fetch jobs, nice -n 15, then gclient runhooks with full Xcode. Writes .gclient on a fresh root.
#
# Usage: sync.sh [--nohooks] [--dry-run]
#   --nohooks  sync only; run hooks later with sync.sh (hooks need Xcode)
#   --dry-run  print the commands, run nothing
# Refuses while a build runs or while Clu is applied (apply.sh --revert first, then apply.sh after the sync).
# Env: CLU_CHROMIUM_ROOT (gclient root, default ~/Developer/clu-chromium.noindex), CLU_CHROMIUM_SRC (default ROOT/src).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

hooks=1
dry=0
for arg in "$@"; do
  case "$arg" in
    --nohooks) hooks=0 ;;
    --dry-run) dry=1 ;;
    -h | --help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument '$arg' (see --help)" ;;
  esac
done

version=$(pinned_version)
run() {
  if [[ $dry -eq 1 ]]; then say "+ $*"; else "$@"; fi
}

[[ -d "$CLU_CHROMIUM_ROOT" ]] || die "no gclient root at $CLU_CHROMIUM_ROOT (mkdir it and clone depot_tools into it)"
use_depot_tools
if [[ -d "$SRC" ]]; then
  [[ ! -e "$SRC/$MANIFEST_NAME" ]] || die "Clu is applied to $SRC; run scripts/chromium/apply.sh --revert first"
  assert_no_build
fi

gclient_file="$CLU_CHROMIUM_ROOT/.gclient"
if [[ ! -f "$gclient_file" ]]; then
  say "$([[ $dry -eq 1 ]] && echo 'would write' || echo writing) $gclient_file (Mac only, no PGO profiles; release builds need them, PLAN 3.1 step 4)"
  [[ $dry -eq 1 ]] || cat >"$gclient_file" <<'EOF'
solutions = [
  {
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {"checkout_pgo_profiles": False},
  },
]
target_os = ["mac"]
EOF
fi

cd "$CLU_CHROMIUM_ROOT"
say "syncing Chromium $version into $CLU_CHROMIUM_ROOT/src"
run nice -n 15 gclient sync --nohooks --no-history --shallow -j 4 --revision "src@refs/tags/$version"
if [[ $hooks -eq 1 ]]; then
  use_xcode
  run nice -n 15 gclient runhooks
fi
[[ $dry -eq 0 ]] || exit 0

got=$(awk -F= '{ v = v (v == "" ? "" : ".") $2 } END { print v }' "$SRC/chrome/VERSION")
[[ "$got" == "$version" ]] || die "src/chrome/VERSION says $got, expected $version"
say "Chromium $version synced$([[ $hooks -eq 1 ]] && echo ' with hooks')"
