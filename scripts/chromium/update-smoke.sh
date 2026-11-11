#!/usr/bin/env bash
# Proves a Sparkle update from one local test build to a newer one through a feed on 127.0.0.1 (docs/chromium/m12.md,
# PLAN M12 "updates to a newer test build through Sparkle"). It packages build A and build B (A with its version
# raised) with package.sh, ad-hoc signed, with a fresh EdDSA key and a feed on this Mac; serves B's archive and
# appcast from 127.0.0.1 only; installs A from its DMG into a temporary Applications folder; lets Sparkle find,
# download, verify and install B; and checks that the installed app is now B, still verifies, and that the default
# browser did not change.
#
# Usage: update-smoke.sh [--app PATH]
#   --app   a built Rio.app (out/Release). Rio itself runs, headless with a fresh profile, and its updater
#           (CluUpdateService on Sparkle) downloads B; quitting Rio installs it.
#   none    a stand-in bundle packaged the same way (same Sparkle, Info.plist keys, signature and appcast), and
#           sparkle_drive.py runs Sparkle's updater for it in a plain process: no Chromium build needed.
# Everything lives in a temporary directory; the server, Rio, LaunchServices entries, Sparkle's preferences and caches
# for the test bundle are all removed at the end.
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

app=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) [[ $# -ge 2 && -n "$2" ]] || die "--app needs a path"; app=${2%/}; shift ;;
    -h | --help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option '$1' (see --help)" ;;
  esac
  shift
done
[[ -z "$app" || -d "$app" ]] || die "no app at $app"

SCRIPTS="$REPO/scripts/chromium"
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
SMOKE_ID=$BUNDLE_ID.sparkle-smoke
tmp=$(mktemp -d "${TMPDIR:-/tmp}/clu-update-smoke.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
server_pid=
clu_pid=
bundle_id=$SMOKE_ID
cleanup() {
  [[ -z "$clu_pid" ]] || kill "$clu_pid" 2>/dev/null || true
  [[ -z "$server_pid" ]] || kill "$server_pid" 2>/dev/null || true
  hdiutil detach -quiet "$tmp/mnt" 2>/dev/null || true
  "$LSREGISTER" -dump 2>/dev/null | sed -n 's/^path: *//p' | sed 's/ (0x[0-9a-f]*)$//' | grep -F "$tmp/" | sort -u |
    while IFS= read -r path; do "$LSREGISTER" -u "$path" 2>/dev/null || true; done || true
  if [[ "$bundle_id" == "$SMOKE_ID" ]]; then
    defaults delete "$SMOKE_ID" 2>/dev/null || true
    rm -f "$HOME/Library/Preferences/$SMOKE_ID.plist"
    rm -rf "$HOME/Library/Caches/$SMOKE_ID" "$HOME/Library/HTTPStorages/$SMOKE_ID"
  else
    # The test id's domain (out/Dev shares it): only Sparkle's keys, and only Sparkle's cache.
    for k in SUHasLaunchedBefore SULastCheckTime SUUpdateGroupIdentifier SUSkippedVersion SUSkippedMinorVersion \
      SUSkippedMajorVersion; do
      defaults delete "$bundle_id" "$k" 2>/dev/null || true
    done
    rm -rf "$HOME/Library/Caches/$bundle_id/org.sparkle-project.Sparkle"
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

plist() { plutil -extract "$1" raw -o - "$2" 2>/dev/null || true; }
handlers() { defaults read com.apple.LaunchServices/com.apple.launchservices.secure LSHandlers 2>/dev/null | shasum | cut -d' ' -f1; }

# A stand-in for a Rio build: Apple's true(1) as the arm64 executable, Rio's name, a test bundle id.
make_stand_in() {
  local dest=$1 version=$2 short=$3
  mkdir -p "$dest/Contents/MacOS" "$dest/Contents/Frameworks"
  cp /usr/bin/true "$dest/Contents/MacOS/Rio"
  cat >"$dest/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>Rio</string>
  <key>CFBundleIdentifier</key><string>$SMOKE_ID</string>
  <key>CFBundleName</key><string>Rio</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$short</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
</dict>
</plist>
PLIST
}

a_src="$tmp/a/Rio.app"
b_src="$tmp/b/Rio.app"
if [[ -n "$app" ]]; then
  version=$(plist CFBundleVersion "$app/Contents/Info.plist")
  short=$(plist CFBundleShortVersionString "$app/Contents/Info.plist")
  mkdir -p "$tmp/a" "$tmp/b"
  ditto "$app" "$a_src"
  ditto "$app" "$b_src"
  # Both copies run under the test id, never the installed Rio's.
  use_test_bundle_id "$a_src"
  use_test_bundle_id "$b_src"
  bundle_id=$(plist CFBundleIdentifier "$a_src/Contents/Info.plist")
else
  version=8127.18
  short=158.0.8127.18
  make_stand_in "$a_src" "$version" "$short"
  make_stand_in "$b_src" "$version" "$short"
fi
# B is A one step newer; package.sh signs it again.
new_version="$version.1"
new_short="$short.1"
plutil -replace CFBundleVersion -string "$new_version" "$b_src/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$new_short" "$b_src/Contents/Info.plist"

# The feed: this Mac only, started first so both packages carry its URL.
feed_root="$tmp/feed"
mkdir -p "$feed_root"
node "$SCRIPTS/sparkle.mjs" serve --root "$feed_root" --log "$tmp/server.log" >"$tmp/server.out" &
server_pid=$!
for _ in $(seq 1 50); do
  grep -q '^listening ' "$tmp/server.out" 2>/dev/null && break
  sleep 0.1
done
port=$(sed -n 's/^listening //p' "$tmp/server.out")
[[ -n "$port" ]] || die "the feed server did not start"
feed="http://127.0.0.1:$port/appcast.xml"

key="$tmp/ed25519.key"
node "$SCRIPTS/sparkle.mjs" keygen "$key" >/dev/null
package() {
  env CLU_PACKAGE_ENV="$tmp/no-package.env" CLU_SIGN_IDENTITY= CLU_NOTARY_PROFILE= CLU_APPCAST_FROM= \
    CLU_UPDATE_BASE_URL= CLU_UPDATE_FEED_URL="$feed" CLU_UPDATE_KEY_FILE="$key" \
    "$SCRIPTS/package.sh" --app "$1" --out "$2"
}
say "packaging A ($short) and B ($new_short)"
package "$a_src" "$tmp/pkg-a" >"$tmp/package-a.log"
package "$b_src" "$tmp/pkg-b" >"$tmp/package-b.log"
cp "$tmp/pkg-b/Rio-$new_short.zip" "$tmp/pkg-b/appcast.xml" "$feed_root/"

# Install A the way a user does: from its image.
mkdir -p "$tmp/mnt" "$tmp/Applications"
hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$tmp/mnt" "$tmp/pkg-a/Rio.dmg"
ditto "$tmp/mnt/Rio.app" "$tmp/Applications/Rio.app"
hdiutil detach -quiet "$tmp/mnt"
installed="$tmp/Applications/Rio.app"
[[ "$(plist CFBundleVersion "$installed/Contents/Info.plist")" == "$version" ]] || die "A did not install"
before=$(handlers)

if [[ -n "$app" ]]; then
  say "running Rio $short headless; its updater should fetch $new_short"
  exe=$(plist CFBundleExecutable "$installed/Contents/Info.plist")
  "$installed/Contents/MacOS/$exe" --headless --user-data-dir="$tmp/profile" --no-first-run --no-default-browser-check \
    --use-mock-keychain --disable-features=DialMediaRouteProvider about:blank >"$tmp/clu.log" 2>&1 &
  clu_pid=$!
  for _ in $(seq 1 240); do
    grep -q "GET /Rio-$new_short.zip 200" "$tmp/server.log" 2>/dev/null && break
    sleep 0.5
  done
  grep -q "GET /Rio-$new_short.zip 200" "$tmp/server.log" 2>/dev/null || die "Rio never downloaded the update (see $tmp/clu.log)"
  # Sparkle verifies and stages the update after the download; then quitting Rio installs it.
  sleep 10
  kill -TERM "$clu_pid"
  wait "$clu_pid" 2>/dev/null || true
  clu_pid=
else
  say "running Sparkle's updater for the installed stand-in"
  if ! /usr/bin/python3 -I "$SCRIPTS/sparkle_drive.py" "$installed" 90 | sed 's/^/  sparkle: /'; then
    die "Sparkle did not take the update"
  fi
fi

for _ in $(seq 1 120); do
  [[ "$(plist CFBundleVersion "$installed/Contents/Info.plist")" == "$new_version" ]] && break
  sleep 0.5
done
got=$(plist CFBundleVersion "$installed/Contents/Info.plist")
[[ "$got" == "$new_version" ]] || die "the installed app is still $got, expected $new_version"
codesign --verify --deep --strict "$installed" || die "the updated app does not verify"
[[ "$(handlers)" == "$before" ]] || die "the default browser handlers changed"
grep -q "GET /appcast.xml 200" "$tmp/server.log" || die "the feed was never read"
say "updated $short -> $new_short through http://127.0.0.1:$port; the app verifies; default browser unchanged"
