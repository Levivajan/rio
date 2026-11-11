#!/usr/bin/env bash
# Builds Clu from the applied checkout (docs/chromium/PLAN.md 2.3, 3.3 to 3.5): gn gen with chromium/args/<profile>.gn,
# then autoninja at low priority. Logs the elapsed time to docs/chromium/build-log.md.
#
# Usage: build.sh [--away] [-j N] [--out DIR] [--dry-run] dev|fast|release [targets...]   (default target: chrome)
#   dev       chromium/args/dev.gn into out/Dev; fast: chromium/args/fast.gn into out/Fast (the daily-use app)
#   release   chromium/args/release.gn into out/Release
#   default   nice -n 10, -j 10: leaves the efficiency cores and some performance cores to whoever uses the Mac
#   --away    caffeinate -i, nice -n 5, -j 14, for overnight builds; ask before starting one
#   --dry-run print the checks and commands, run nothing
# Refuses without apply.sh's manifest, while another build runs, or with less than 60 GB free.
# Env: CLU_CHROMIUM_ROOT (gclient root, default ~/Developer/clu-chromium.noindex), CLU_CHROMIUM_SRC (default ROOT/src),
# CLU_BUNDLE_ID (the app's id, default org.example.rio; also read from ~/.clu/package.env, see common.sh).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

MIN_FREE_GB=60
jobs=10
niceness=10
away=0
dry=0
out=
profile=
targets=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --away) away=1 jobs=14 niceness=5 ;;
    -j) [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "-j needs a positive number"; jobs=$2; shift ;;
    --out) [[ $# -ge 2 && -n "$2" ]] || die "--out needs a directory"; out=$2; shift ;;
    --dry-run) dry=1 ;;
    -h | --help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "unknown option '$1' (see --help)" ;;
    *) if [[ -z "$profile" ]]; then profile=$1; else targets+=("$1"); fi ;;
  esac
  shift
done
case "$profile" in
  dev) out=${out:-out/Dev} ;;
  fast) out=${out:-out/Fast} ;;
  release) out=${out:-out/Release} ;;
  '') die "say which profile: build.sh dev|fast|release (see --help)" ;;
  *) die "unknown profile '$profile' (dev, fast or release)" ;;
esac
[[ ${#targets[@]} -gt 0 ]] || targets=(chrome)
args_file="$CHROMIUM_DIR/args/$profile.gn"
[[ -f "$args_file" ]] || die "missing $args_file"

# In a dry run a failed check is reported and the run goes on, so one run shows every problem.
problems=0
refuse() {
  if [[ $dry -eq 1 ]]; then
    say "would refuse: $*"
    problems=$((problems + 1))
  else
    die "$*"
  fi
}

[[ -d "$SRC" ]] || die "no Chromium checkout at $SRC (run sync.sh, or set CLU_CHROMIUM_SRC)"
[[ -e "$SRC/$MANIFEST_NAME" ]] || refuse "Clu is not applied to $SRC; run scripts/chromium/apply.sh first"
[[ -d "$XCODE_DEVELOPER_DIR" ]] || refuse "Xcode is not at $XCODE_DEVELOPER_DIR (PLAN 3.1 step 1)"
[[ -x "$CLU_CHROMIUM_ROOT/depot_tools/autoninja" ]] || refuse "no depot_tools at $CLU_CHROMIUM_ROOT/depot_tools"
[[ ! -e "$BUILD_LOCK" ]] || refuse "a build holds $BUILD_LOCK ($(cat "$BUILD_LOCK" 2>/dev/null)); delete it only if no build runs"
builds=$(running_builds)
[[ -z "$builds" ]] || refuse "a Chromium build is already running (one at a time, PLAN 3.5):
$builds"
free_gb=$(($(df -Pk "$SRC" | awk 'NR == 2 { print $4 }') / 1024 / 1024))
[[ $free_gb -ge $MIN_FREE_GB ]] || refuse "only $free_gb GB free on the build disk; a build needs $MIN_FREE_GB GB (PLAN 3.4)"
if grep -Eq '^[[:space:]]*chrome_pgo_phase[[:space:]]*=[[:space:]]*2' "$args_file" &&
  ! compgen -G "$SRC/chrome/build/pgo_profiles/*.profdata" >/dev/null; then
  refuse "$profile.gn wants PGO profiles and $SRC/chrome/build/pgo_profiles has none; set checkout_pgo_profiles to True in .gclient and run sync.sh (PLAN 3.1 step 4)"
fi

gn_args=$(cat "$args_file")
# //clu/resources packs the WebUI bundles from the repo's out/webui (chromium/clu/build/config.gni); they never get
# copied into the checkout, so apply.sh keeps a clean tree.
gn_args+=$'\n'"clu_webui_dir = \"$REPO/out/webui\""
keys="$HOME/.clu/keys.gn"
if [[ -f "$keys" ]]; then
  gn_args+=$'\n'$(cat "$keys")
  say "adding the API keys from $keys"
fi
# The app's CFBundleIdentifier is BUNDLE_ID (common.sh); out/Dev's is BUNDLE_ID.test, so a test build never shares the
# installed Rio's id and macOS never hands it the user's links. An id other than BRANDING's placeholder also goes into a
# copy of BRANDING (branding_file_path), so the helpers, the framework and MAC_BUNDLE_IDENTIFIER_STRING carry it too.
# The copy lives in this repo's out folder, never the build dir: gn rejects an input under the build dir that no target
# generates.
app_id=$BUNDLE_ID
[[ "$profile" != dev ]] || app_id=$BUNDLE_ID.test
gn_args+=$'\n'"clu_bundle_id = \"$app_id\""
branding_src="$SRC/chrome/app/theme/chromium/BRANDING"
branding_copy=
if [[ -f "$branding_src" ]] && ! grep -qx "MAC_BUNDLE_ID=$BUNDLE_ID" "$branding_src"; then
  branding_copy="$REPO/out/branding/$(basename "$out")/BRANDING"
  gn_args+=$'\n'"branding_file_path = \"$branding_copy\""
fi
# Every build is a new CFBundleVersion, Chromium's BUILD.PATCH plus the repo's commit count (8127.18.<count>),
# so LaunchServices never takes an old build's icon for this one and Sparkle sees each build as newer.
commits=$(git -C "$REPO" rev-list --count HEAD 2>/dev/null) || die "cannot count the repo's commits for CFBundleVersion"
pinned=$(pinned_version)
bundle_version="${pinned#*.*.}.$commits"
[[ "$bundle_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "bad CFBundleVersion '$bundle_version' from chromium/VERSION '$pinned'"
gn_args+=$'\n'"clu_bundle_version = \"$bundle_version\""
runner=(nice -n "$niceness")
[[ $away -eq 0 ]] || runner=(caffeinate -i nice -n "$niceness")
webui=0
! grep -q '"build:webui"' "$REPO/package.json" || webui=1

if [[ $dry -eq 1 ]]; then
  say "CFBundleVersion $bundle_version"
  say "clu_bundle_id = \"$app_id\"${branding_copy:+, BRANDING copy $branding_copy with MAC_BUNDLE_ID=$BUNDLE_ID}"
  say "DEVELOPER_DIR=$XCODE_DEVELOPER_DIR, depot_tools from $CLU_CHROMIUM_ROOT/depot_tools, $free_gb GB free"
  [[ $webui -eq 0 ]] || say "+ node scripts/chromium/fetch-filter-lists.mjs && npm run build:webui"
  say "+ (cd $SRC && gn gen $out --args=<$args_file>)"
  say "+ (cd $SRC && ${runner[*]} autoninja -C $out -j $jobs ${targets[*]})"
  [[ $problems -eq 0 ]] || exit 1
  exit 0
fi

use_xcode
use_depot_tools
set -o noclobber
{ printf 'pid %s, build.sh %s %s, since %s\n' "$$" "$profile" "$out" "$(date -u +%FT%TZ)" >"$BUILD_LOCK"; } 2>/dev/null ||
  die "a build holds $BUILD_LOCK"
set +o noclobber
trap 'rm -f "$BUILD_LOCK"' EXIT

# The blocker is compiled from EasyList and EasyPrivacy (docs/chromium/m7.md); offline, the last copy is used.
[[ $webui -eq 0 ]] || (cd "$REPO" && { node scripts/chromium/fetch-filter-lists.mjs || say "filter lists unavailable; the blocker carries the fixture list"; } && npm run build:webui)
if [[ -n "$branding_copy" ]]; then
  mkdir -p "$(dirname "$branding_copy")"
  sed "s/^MAC_BUNDLE_ID=.*/MAC_BUNDLE_ID=$BUNDLE_ID/" "$branding_src" >"$branding_copy.new"
  # Rewritten only when it changes, so an unchanged id never reruns gn's branding scripts.
  if cmp -s "$branding_copy.new" "$branding_copy"; then rm -f "$branding_copy.new"; else mv "$branding_copy.new" "$branding_copy"; fi
fi
cd "$SRC"
"${runner[@]}" gn gen "$out" --args="$gn_args"
start=$SECONDS
status=0
"${runner[@]}" autoninja -C "$out" -j "$jobs" "${targets[@]}" || status=$?
elapsed=$((SECONDS - start))

log="$REPO/docs/chromium/build-log.md"
mkdir -p "$(dirname "$log")"
if [[ ! -f "$log" ]]; then
  printf '# Chromium build log\n\nWritten by scripts/chromium/build.sh.\n\n| Finished (UTC) | Version | Profile | Out | Targets | Jobs | Elapsed | Exit |\n|---|---|---|---|---|---|---|---|\n' >"$log"
fi
printf '| %s | %s | %s | %s | %s | %s | %dh %02dm | %s |\n' "$(date -u +%FT%TZ)" "$(pinned_version)" "$profile" "$out" \
  "${targets[*]}" "$jobs" $((elapsed / 3600)) $((elapsed % 3600 / 60)) "$status" >>"$log"
say "autoninja exited $status after $((elapsed / 60)) minutes; logged to docs/chromium/build-log.md"
exit "$status"
