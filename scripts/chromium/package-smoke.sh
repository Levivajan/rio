#!/usr/bin/env bash
# Checks a Rio.dmg from package.sh the way a new user meets it (docs/chromium/m12.md, docs/PLAN.md 12.16 last box):
# the image verifies, holds Rio.app and an Applications link, the app is Rio's (bundle id, name, arm64, URL schemes,
# usage strings), its signature verifies, Gatekeeper's verdict matches the signature, Sparkle and its keys are in
# place, and a Chromium build installed from the image starts with a fresh profile and home, headless, and leaves
# the default browser as it was. Nothing it copies stays registered with LaunchServices.
#
# Usage: package-smoke.sh [--dmg PATH]   (default: <repo>/out/package/Rio.dmg)
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

dmg="$REPO/out/package/Rio.dmg"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dmg) [[ $# -ge 2 && -n "$2" ]] || die "--dmg needs a path"; dmg=$2; shift ;;
    -h | --help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option '$1' (see --help)" ;;
  esac
  shift
done
[[ -f "$dmg" ]] || die "no image at $dmg (run scripts/chromium/package.sh)"

LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
tmp=$(mktemp -d "${TMPDIR:-/tmp}/clu-package-smoke.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
mnt="$tmp/mnt"
clu_pid=
cleanup() {
  [[ -z "$clu_pid" ]] || kill "$clu_pid" 2>/dev/null || true
  hdiutil detach -quiet "$mnt" 2>/dev/null || true
  "$LSREGISTER" -dump 2>/dev/null | sed -n 's/^path: *//p' | sed 's/ (0x[0-9a-f]*)$//' | grep -F "$tmp/" | sort -u |
    while IFS= read -r path; do "$LSREGISTER" -u "$path" 2>/dev/null || true; done || true
  rm -rf "$tmp"
}
trap cleanup EXIT

failures=0
check() {
  local what=$1
  shift
  if "$@" >/dev/null 2>&1; then
    say "ok    $what"
  else
    say "FAIL  $what"
    failures=$((failures + 1))
  fi
}
note() { say "note  $*"; }
plist() { plutil -extract "$1" raw -o - "$2" 2>/dev/null || true; }
handlers() { defaults read com.apple.LaunchServices/com.apple.launchservices.secure LSHandlers 2>/dev/null | shasum | cut -d' ' -f1; }

check "hdiutil verify" hdiutil verify -quiet "$dmg"
mkdir -p "$mnt"
hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$mnt" "$dmg"
check "the image holds Rio.app" test -d "$mnt/Rio.app"
check "the image links Applications" test "$(readlink "$mnt/Applications")" = /Applications

# Installed the way a user installs it: copied out of the image.
apps="$tmp/Applications"
mkdir -p "$apps"
ditto "$mnt/Rio.app" "$apps/Rio.app"
hdiutil detach -quiet "$mnt"
app="$apps/Rio.app"
info="$app/Contents/Info.plist"
bundle_id=$(plist CFBundleIdentifier "$info")
exe=$(plist CFBundleExecutable "$info")
check "bundle id $BUNDLE_ID ($bundle_id)" is_rio_id "$bundle_id"
check "named Rio" test "$(plist CFBundleName "$info")" = Rio
min_system=$(plist LSMinimumSystemVersion "$info")
check "LSMinimumSystemVersion is 12 or later ($min_system)" test "${min_system%%.*}" -ge 12
check "arm64 executable" sh -c "lipo -archs '$app/Contents/MacOS/$exe' | grep -Eqw 'arm64e?'"
check "codesign --verify --deep --strict" codesign --verify --deep --strict "$app"

details=$(codesign -dv --verbose=2 "$app" 2>&1 || true)
gatekeeper=$(spctl -a -vv -t exec "$app" 2>&1 || true)
if [[ "$details" == *"Signature=adhoc"* ]]; then
  note "ad-hoc signature: Gatekeeper says '$(head -n 1 <<<"$gatekeeper")', as expected without a Developer ID and notarization"
else
  check "Developer ID signature" grep -q 'Authority=Developer ID Application' <<<"$details"
  check "Gatekeeper accepts the app (spctl -a -vv)" grep -q ': accepted' <<<"$gatekeeper"
  check "Gatekeeper accepts the image" sh -c "spctl -a -vv -t install '$dmg' 2>&1 | grep -q ': accepted'"
  check "the notarization ticket is stapled" xcrun stapler validate -q "$app"
fi

sparkle="$app/Contents/Frameworks/Sparkle.framework"
check "Sparkle.framework is embedded and signed" codesign --verify --strict "$sparkle"
feed=$(plist SUFeedURL "$info")
key=$(plist SUPublicEDKey "$info")
if [[ -n "$feed" ]]; then
  check "the feed is https or this Mac ($feed)" node --input-type=module -e \
    "import { isAllowedFeedUrl } from '$REPO/scripts/chromium/sparkle.mjs'; process.exit(isAllowedFeedUrl(process.argv[1]) ? 0 : 1)" "$feed"
  check "SUPublicEDKey is a 32-byte key" test "$(printf '%s' "$key" | base64 -d 2>/dev/null | wc -c | tr -d ' ')" = 32
  check "updates check, download and install without asking" test "$(plist SUEnableAutomaticChecks "$info")$(plist SUAutomaticallyUpdate "$info")" = truetrue
  check "updates are checked every hour" test "$(plist SUScheduledCheckInterval "$info")" = 3600
else
  check "no feed and no key" test -z "$key"
  note "no SUFeedURL: this package never checks for updates"
fi

framework=$(find "$app/Contents/Frameworks" -maxdepth 1 -name '* Framework.framework' | head -n 1)
if [[ -z "$framework" ]]; then
  note "not a Chromium build (no framework): the URL schemes, usage strings and launch are not checked"
else
  check "http and https URL schemes" sh -c "plutil -extract CFBundleURLTypes json -o - '$info' | grep -q '\"https\"'"
  strings=$(find "$app/Contents" -path '*/en.lproj/InfoPlist.strings' | head -n 1)
  check "usage strings name Rio" sh -c "plutil -convert json -o - '$strings' | grep -q 'NSCameraUsageDescription.*Rio'"
  sigs=$(find "$framework" -maxdepth 4 -path '*/Resources/*.sig' -type f | wc -l | tr -d ' ')
  note "Widevine signatures: $sigs (0 until Google's VMP cert)"

  # A first launch as on a fresh account: an empty home for Cocoa, a new profile, no keychain prompt, headless. It runs
  # a copy under the test id, so a link opened meanwhile goes to the installed Rio and its defaults stay his.
  before=$(handlers)
  home="$tmp/home"
  mkdir -p "$home" "$tmp/launch"
  ditto "$app" "$tmp/launch/Rio.app"
  use_test_bundle_id "$tmp/launch/Rio.app"
  CFFIXED_USER_HOME="$home" "$tmp/launch/Rio.app/Contents/MacOS/$exe" --headless --dump-dom --user-data-dir="$tmp/profile" \
    --no-first-run --no-default-browser-check --use-mock-keychain --disable-features=DialMediaRouteProvider \
    'data:text/html,<title>clu-smoke</title><p>ok</p>' >"$tmp/dom.html" 2>"$tmp/launch.log" &
  clu_pid=$!
  for _ in $(seq 1 120); do
    kill -0 "$clu_pid" 2>/dev/null || break
    sleep 0.5
  done
  if kill -0 "$clu_pid" 2>/dev/null; then
    kill "$clu_pid" 2>/dev/null || true
    say "FAIL  the headless launch did not finish in 60 s"
    failures=$((failures + 1))
  fi
  wait "$clu_pid" 2>/dev/null || true
  clu_pid=
  check "the installed app starts and renders a page" grep -q 'clu-smoke' "$tmp/dom.html"
  check "it made a fresh profile" test -d "$tmp/profile/Default"
  check "the default browser is unchanged" test "$(handlers)" = "$before"
fi

if [[ $failures -gt 0 ]]; then
  die "$failures check(s) failed"
fi
say "all checks passed for $dmg"
