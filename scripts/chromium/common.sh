# shellcheck shell=bash disable=SC2034
# Shared by scripts/chromium/*.sh (docs/chromium/PLAN.md 2.3). Sourced, never run.
# CLU_CHROMIUM_ROOT is the gclient root (it holds .gclient, depot_tools and src). CLU_CHROMIUM_SRC overrides src alone.

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
CHROMIUM_DIR="$REPO/chromium"
CLU_CHROMIUM_ROOT=${CLU_CHROMIUM_ROOT:-$HOME/Developer/clu-chromium.noindex}
SRC=${CLU_CHROMIUM_SRC:-$CLU_CHROMIUM_ROOT/src}
MANIFEST_NAME=.clu-applied
BUILD_LOCK="$(dirname "$SRC")/.build.lock"
XCODE_DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

die() {
  printf '%s: error: %s\n' "${0##*/}" "$*" >&2
  exit 1
}

say() {
  printf '%s: %s\n' "${0##*/}" "$*"
}

pinned_version() {
  local v
  v=$(tr -d '[:space:]' <"$CHROMIUM_DIR/VERSION")
  [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "chromium/VERSION holds '$v', expected a tag like 158.0.8127.18"
  printf '%s\n' "$v"
}

# Refuses while a build owns the checkout: build.sh's lock, or (for the checkout under CLU_CHROMIUM_ROOT) any
# autoninja or siso process, which also catches builds started without build.sh.
assert_no_build() {
  local builds
  [[ ! -e "$BUILD_LOCK" ]] ||
    die "a build holds $BUILD_LOCK ($(cat "$BUILD_LOCK" 2>/dev/null)); wait for it, or delete the lock if no build runs"
  [[ "$(cd "$SRC" && pwd -P)" == "$(cd "$CLU_CHROMIUM_ROOT" 2>/dev/null && pwd -P)/"* ]] || return 0
  builds=$(running_builds)
  [[ -z "$builds" ]] || die "a Chromium build is running; wait for it to end:
$builds"
}

# Prints the command line of every autoninja or siso build on this Mac. Best effort: prints nothing if ps can't list.
# ps, not pgrep: pgrep fails outright under some sandboxes ("Cannot get process list").
running_builds() {
  # shellcheck disable=SC2009
  ps -axo command= 2>/dev/null | grep -E '(depot_tools/autoninja|siso(_[a-z]+)? ninja)' | grep -v grep || true
}

use_depot_tools() {
  [[ -x "$CLU_CHROMIUM_ROOT/depot_tools/gclient" ]] ||
    die "no depot_tools at $CLU_CHROMIUM_ROOT/depot_tools (git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git there)"
  export PATH="$CLU_CHROMIUM_ROOT/depot_tools:$PATH" DEPOT_TOOLS_UPDATE=0 DEPOT_TOOLS_METRICS=0
}

use_xcode() {
  [[ -d "$XCODE_DEVELOPER_DIR" ]] || die "Xcode is not at $XCODE_DEVELOPER_DIR (PLAN 3.1 step 1)"
  export DEVELOPER_DIR="$XCODE_DEVELOPER_DIR"
}

# ---- self-signed code signing (docs/chromium/m12.md section 7) -------------------------------------------------------
# A self-signed identity (CLU_SIGN_IDENTITY) in its own keychain (CLU_SIGN_KEYCHAIN, its password in keychain-password
# beside it) gives every build the same designated requirement, so macOS keeps treating an update as the same app.
# codesign finds an identity only in a keychain on its search list, which it reads from
# $HOME/Library/Preferences/com.apple.security.plist; it runs here with a private HOME whose list holds only the signing
# keychain, so the user's own search list, default keychain and trust settings are never changed. codesign signs with
# the untrusted identity; only Gatekeeper asks for trust.
# Reads ~/.clu/package.env (KEY=VALUE lines; CLU_PACKAGE_ENV names another file) into the named CLU_* variables the
# environment leaves unset: the environment wins over the file.
load_package_env() { # KEY...
  local env_file=${CLU_PACKAGE_ENV:-$HOME/.clu/package.env} line key value k
  [[ -f "$env_file" ]] || return 0
  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ "$line" =~ ^(CLU_[A-Z_]+)=(.*)$ ]] || continue
    key=${BASH_REMATCH[1]}
    value=${BASH_REMATCH[2]}
    value=${value#\"}
    value=${value%\"}
    for k in "$@"; do
      if [[ "$k" == "$key" && -z "${!k:-}" ]]; then
        printf -v "$k" '%s' "$value"
        export "${k?}"
      fi
    done
  done <"$env_file"
}

# The build's identity. The tree carries placeholders; the official build sets its own in ~/.clu/package.env:
#   CLU_BUNDLE_ID        the app's CFBundleIdentifier (default org.example.rio; test copies add .test)
#   CLU_SIGN_IDENTIFIER  the code-signing identifier of a self-signed app (default the bundle id), kept stable so the
#                        designated requirement, and with it the keychain's trust and Sparkle's same-app check, never moves
#   CLU_RELEASES_REPO    the GitHub repository publish-release.sh publishes to (no default)
load_package_env CLU_BUNDLE_ID CLU_SIGN_IDENTIFIER CLU_RELEASES_REPO
BUNDLE_ID=${CLU_BUNDLE_ID:-org.example.rio}
SIGN_IDENTIFIER=${CLU_SIGN_IDENTIFIER:-$BUNDLE_ID}
[[ "$BUNDLE_ID" =~ ^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$ ]] || die "CLU_BUNDLE_ID '$BUNDLE_ID' is not a reverse-DNS bundle id"
[[ "$SIGN_IDENTIFIER" =~ ^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$ ]] ||
  die "CLU_SIGN_IDENTIFIER '$SIGN_IDENTIFIER' is not a reverse-DNS identifier"
# Whether $1 is this build's bundle id or one of its suffixed variants (.test, .sparkle-smoke).
is_rio_id() { [[ "$1" == "$BUNDLE_ID" || "$1" == "$BUNDLE_ID".* ]]; }

# Sparkle, thinned to arm64, into an app (it loads it at run time, clu_sparkle_update_driver_mac.mm), and the update
# keys into its Info.plist: with a feed, the feed, the EdDSA public key and quiet automatic updates; without one, no
# update keys, so the app carries Sparkle and never checks (docs/chromium/m12.md).
embed_sparkle() { # app sparkle-dir feed public-key
  local app=$1 sparkle=$2 feed=$3 public_key=$4 info="$1/Contents/Info.plist" k
  rm -rf "$app/Contents/Frameworks/Sparkle.framework"
  mkdir -p "$app/Contents/Frameworks"
  ditto --arch arm64 "$sparkle/Sparkle.framework" "$app/Contents/Frameworks/Sparkle.framework"
  for k in SUFeedURL SUPublicEDKey SUEnableAutomaticChecks SUScheduledCheckInterval SUAutomaticallyUpdate \
    SUAllowsAutomaticUpdates SUVerifyUpdateBeforeExtraction SUEnableSystemProfiling; do
    plutil -remove "$k" "$info" 2>/dev/null || true
  done
  if [[ -n "$feed" ]]; then
    plutil -replace SUFeedURL -string "$feed" "$info"
    plutil -replace SUPublicEDKey -string "$public_key" "$info"
    # Checks without asking, downloads in the background, installs on quit or on Relaunch; nothing about the Mac is sent.
    plutil -replace SUEnableAutomaticChecks -bool YES "$info"
    # Every hour (Sparkle's shortest), not its default day, so testers get a fix the same evening.
    plutil -replace SUScheduledCheckInterval -integer 3600 "$info"
    plutil -replace SUAutomaticallyUpdate -bool YES "$info"
    plutil -replace SUAllowsAutomaticUpdates -bool YES "$info"
    plutil -replace SUVerifyUpdateBeforeExtraction -bool YES "$info"
    plutil -replace SUEnableSystemProfiling -bool NO "$info"
  fi
}

SIGN_IDENTITY=
SIGN_KEYCHAIN=
SIGN_HOME=
SIGN_PROBLEM=
# Chromium's own entitlements (chrome/installer/mac/signing/parts.py signs the app and the helpers with them).
SIGN_ENTITLEMENTS="$SRC/chrome/app"

# Unlocks KEYCHAIN for self_sign, or sets SIGN_PROBLEM to what keeps IDENTITY in it from signing and returns 1.
open_signing_keychain() {
  SIGN_PROBLEM=
  local identity=$1 keychain=$2 password="${2%/*}/keychain-password" f
  [[ -n "$identity" ]] || { SIGN_PROBLEM="CLU_SIGN_KEYCHAIN needs CLU_SIGN_IDENTITY, the identity's name"; return 1; }
  [[ "$identity" != "Developer ID Application:"* ]] ||
    { SIGN_PROBLEM="a Developer ID identity is signed through sign_chrome.py: unset CLU_SIGN_KEYCHAIN"; return 1; }
  [[ -f "$keychain" ]] || { SIGN_PROBLEM="no signing keychain at $keychain"; return 1; }
  [[ -f "$password" ]] || { SIGN_PROBLEM="no keychain password at $password"; return 1; }
  [[ "$(stat -f %Lp "$password")" == 600 ]] || { SIGN_PROBLEM="$password must have mode 600"; return 1; }
  for f in app helper-renderer helper-gpu; do
    [[ -f "$SIGN_ENTITLEMENTS/$f-entitlements.plist" ]] || { SIGN_PROBLEM="missing $SIGN_ENTITLEMENTS/$f-entitlements.plist (the Chromium checkout)"; return 1; }
  done
  if [[ -z "$SIGN_HOME" ]]; then
    SIGN_HOME=$(mktemp -d "${TMPDIR:-/tmp}/clu-sign-home.XXXXXX")
    mkdir -p "$SIGN_HOME/Library/Preferences"
  fi
  HOME=$SIGN_HOME security list-keychains -d user -s "$keychain" || { SIGN_PROBLEM="cannot list $keychain"; return 1; }
  HOME=$SIGN_HOME security unlock-keychain -p "$(cat "$password")" "$keychain" 2>/dev/null ||
    { SIGN_PROBLEM="cannot unlock $keychain with $password"; return 1; }
  HOME=$SIGN_HOME security find-identity -p codesigning "$keychain" | grep -Fq "\"$identity\"" ||
    { SIGN_PROBLEM="the keychain $keychain has no code signing identity \"$identity\""; return 1; }
  SIGN_IDENTITY=$identity
  SIGN_KEYCHAIN=$keychain
}

close_signing_keychain() {
  [[ -z "$SIGN_HOME" ]] || rm -rf "$SIGN_HOME"
  SIGN_HOME=
}

# codesign with the self-signed identity, quiet unless it fails ("replacing existing signature" is every call here).
self_sign() {
  local log
  log=$(HOME=$SIGN_HOME codesign --force --sign "$SIGN_IDENTITY" --keychain "$SIGN_KEYCHAIN" --timestamp=none "$@" 2>&1) || {
    printf '%s\n' "$log" >&2
    return 1
  }
}

# Signs an app inside out: loose Mach-O files, then nested bundles deepest first, then the app, so each seal covers
# signed code. The renderer and GPU helpers and the app get Chromium's entitlements; other code keeps its own (Sparkle's
# Downloader.xpc). No hardened runtime: it turns on library validation, which needs a Team ID that a self-signed
# certificate does not have, so the app could not load its own framework.
sign_self_signed() {
  local app=$1 bundle_id f kind
  bundle_id=$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist")
  while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    if [[ "${f##*/}" == web_app_shortcut_copier ]]; then
      self_sign --identifier "$bundle_id.web_app_shortcut_copier" "$f"
    else
      self_sign --preserve-metadata=entitlements "$f"
    fi
  done < <(find "$app/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0)
  find "$app/Contents" -type d \( -name '*.app' -o -name '*.framework' -o -name '*.xpc' -o -name '*.appex' \
    -o -name '*.bundle' -o -name '*.plugin' -o -name '*.docktileplugin' \) |
    awk -F/ '{ print NF "\t" $0 }' | sort -rn -k1,1 | cut -f2- | while IFS= read -r f; do
      case "${f##*/}" in
        *" Helper (Renderer).app" | *" Helper (Aperitif Renderer).app") kind=helper-renderer ;;
        *" Helper (GPU).app" | *" Helper (Aperitif GPU).app") kind=helper-gpu ;;
        *) kind= ;;
      esac
      if [[ -n "$kind" ]]; then
        self_sign --entitlements "$SIGN_ENTITLEMENTS/$kind-entitlements.plist" "$f"
      else
        self_sign --preserve-metadata=entitlements "$f"
      fi
    done
  # The app signs as SIGN_IDENTIFIER plus its id's suffix, so its designated requirement stays the same from build to
  # build: the login keychain's "Rio Safe Storage" item (keyed by name, not id) trusts it without a prompt, and Sparkle
  # sees an update as the same app.
  self_sign --identifier "$SIGN_IDENTIFIER${bundle_id#"$BUNDLE_ID"}" \
    --entitlements "$SIGN_ENTITLEMENTS/app-entitlements.plist" "$app"
}

# A test launch never carries the installed Rio's bundle id, or macOS hands the user's links to the hidden test
# window and the test writes into their defaults domain. Gives a copy of a production app the test id and signs it ad hoc
# again (package.sh signs it properly when it packages the copy). Never call it on ~/Applications/Rio.app.
RIO_TEST_ID=$BUNDLE_ID.test
use_test_bundle_id() { # app
  local app=$1 info="$1/Contents/Info.plist"
  [[ "$app" != "${RIO_APPS:-$HOME/Applications}/"* ]] || die "refusing to change the installed Rio ($app)"
  [[ "$(plutil -extract CFBundleIdentifier raw -o - "$info")" == "$BUNDLE_ID" ]] || return 0
  plutil -replace CFBundleIdentifier -string "$RIO_TEST_ID" "$info"
  codesign --force --deep --preserve-metadata=entitlements --sign - "$app" >/dev/null 2>&1 ||
    die "could not sign the test copy $app"
}

# Unregisters from LaunchServices every Rio.app outside ~/Applications (out/Dev, out/Release, test copies),
# so the Dock and Finder never take an old build's icon for the installed Rio. The installed one stays registered.
unregister_stray_rio() {
  local ls=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
  local apps="${RIO_APPS:-$HOME/Applications}" path
  "$ls" -dump 2>/dev/null | sed -n 's/^path: *//p' | sed 's/ (0x[0-9a-f]*)$//' | grep -E '/Rio\.app/?$' | sort -u |
    while IFS= read -r path; do
      [[ "$path" == "$apps/"* ]] || "$ls" -u "$path" 2>/dev/null || true
    done || true
}
