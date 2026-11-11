#!/usr/bin/env bash
# Signs a built Rio.app in place. With CLU_SIGN_KEYCHAIN: the self-signed identity, so every build keeps the same
# designated requirement and macOS keeps its keychain grants across updates (docs/chromium/m12.md
# section 7). Without it: a Developer ID and the browser passkey entitlement (docs/chromium/m9.md section 4). The
# identities and the provisioning profile stay on the signing Mac and are never committed.
#
# Usage: sign-dev.sh [--dry-run] [path/to/Rio.app]   (default: out/Release/Rio.app in the checkout)
#   --dry-run  print the checks and the codesign commands, change nothing
# Env: CLU_SIGN_IDENTITY  the identity: a self-signed one with a keychain, else "Developer ID Application: <name> (<team>)"
#      CLU_SIGN_KEYCHAIN  the keychain holding the self-signed identity (~/.clu/signing/rio-signing.keychain-db); its
#                         password is in keychain-password beside it. No notarization, no passkey entitlement.
#      CLU_SIGN_PROFILE   the provisioning profile with the passkey entitlement (default ~/.clu/signing/Rio.provisionprofile)
#      CLU_UPDATE_FEED_URL, CLU_UPDATE_KEY_FILE  with a keychain and a feed, Sparkle and the update keys go into the app
#                         before it is signed, as package.sh does, so an installed build updates itself
# Unset variables are read from ~/.clu/package.env, as package.sh reads them.
# A Developer ID signature signs helpers and libraries with --deep, which is for local use only; a build Rio ships is
# signed by package.sh (M12).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

dry=0
app=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) dry=1 ;;
    -h | --help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "unknown option '$1' (see --help)" ;;
    *) [[ -z "$app" ]] || die "one app at a time"; app=$1 ;;
  esac
  shift
done
app=${app:-$SRC/out/Release/Rio.app}
app=${app%/}
load_package_env CLU_SIGN_IDENTITY CLU_SIGN_KEYCHAIN CLU_SIGN_PROFILE CLU_UPDATE_FEED_URL CLU_UPDATE_KEY_FILE
identity=${CLU_SIGN_IDENTITY:-}
feed=${CLU_UPDATE_FEED_URL:-}
key_file=${CLU_UPDATE_KEY_FILE:-$HOME/.clu/sparkle/ed25519.key}
SPARKLE_JS="$REPO/scripts/chromium/sparkle.mjs"
keychain=${CLU_SIGN_KEYCHAIN:-}
profile=${CLU_SIGN_PROFILE:-$HOME/.clu/signing/Rio.provisionprofile}
template="$CHROMIUM_DIR/clu/app/app-entitlements-clu.plist"
decoded=
entitlements=
trap 'rm -f "$decoded" "$entitlements"; close_signing_keychain' EXIT

problems=0
refuse() {
  if [[ $dry -eq 1 ]]; then
    say "would refuse: $*"
    problems=$((problems + 1))
  else
    die "$*"
  fi
}

[[ -d "$app/Contents/MacOS" ]] || refuse "no app bundle at $app (build it first)"

if [[ -n "$keychain" ]]; then
  [[ ! -f "$app/Contents/Info.plist" ]] || is_rio_id "$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist")" ||
    refuse "the app's bundle id is not this build's ($BUNDLE_ID, CLU_BUNDLE_ID)"
  open_signing_keychain "$identity" "$keychain" || refuse "$SIGN_PROBLEM"
  public_key=
  if [[ -n "$feed" ]]; then
    node --input-type=module -e "import { isAllowedFeedUrl } from '$SPARKLE_JS'; process.exit(isAllowedFeedUrl(process.argv[1]) ? 0 : 1)" "$feed" ||
      refuse "CLU_UPDATE_FEED_URL must be https, or http to 127.0.0.1, ::1 or localhost: $feed"
    public_key=$(node "$SPARKLE_JS" pubkey "$key_file" 2>&1) || refuse "the update key: ${public_key#sparkle.mjs: error: }"
  fi
  steps=()
  [[ -z "$feed" ]] || steps+=("embed Sparkle.framework (arm64); Info.plist SUFeedURL $feed, SUPublicEDKey from $key_file")
  steps+=(
    "codesign --force --sign '$identity' --keychain $keychain --timestamp=none (every Mach-O file and bundle, inside out; Chromium's entitlements for the app and the renderer and GPU helpers)"
    "codesign --verify --deep --strict $app"
    "codesign -d -r- $app"
  )
  if [[ $dry -eq 1 ]]; then
    for step in "${steps[@]}"; do say "+ $step"; done
    [[ $problems -eq 0 ]] || exit 1
    exit 0
  fi
  rm -f "$app/Contents/embedded.provisionprofile"
  if [[ -n "$feed" ]]; then
    embed_sparkle "$app" "$("$REPO/scripts/chromium/fetch-sparkle.sh")" "$feed" "$public_key"
    say "embedded Sparkle with the feed $feed"
  fi
  sign_self_signed "$app"
  codesign --verify --deep --strict "$app"
  requirement=$(codesign -d -r- "$app" 2>&1 | sed -n 's/^designated => //p')
  [[ -n "$requirement" && "$requirement" != *cdhash* ]] || die "the signed app has no stable designated requirement: $requirement"
  say "signed $app with '$identity' (self-signed, no passkey entitlement)"
  say "designated requirement: $requirement"
  exit 0
fi
[[ -f "$template" ]] || refuse "missing $template"
[[ -n "$identity" ]] || refuse "set CLU_SIGN_IDENTITY to the Developer ID Application identity (docs/chromium/m9.md 4)"
if [[ -n "$identity" ]] && ! security find-identity -v -p codesigning | grep -Fq "$identity"; then
  refuse "the login keychain has no valid signing identity '$identity'"
fi
[[ -f "$profile" ]] || refuse "no provisioning profile at $profile (docs/chromium/m9.md 4)"

team=
bundle_id=
if [[ -f "$profile" ]]; then
  decoded=$(mktemp)
  security cms -D -i "$profile" >"$decoded" 2>/dev/null || refuse "cannot read $profile"
  team=$(plutil -extract TeamIdentifier.0 raw -o - "$decoded" 2>/dev/null || true)
  [[ "$team" =~ ^[A-Z0-9]{10}$ ]] || refuse "the profile has no team identifier"
  plutil -extract Entitlements.com\\.apple\\.developer\\.web-browser\\.public-key-credential raw -o - "$decoded" \
    >/dev/null 2>&1 || refuse "the profile does not grant com.apple.developer.web-browser.public-key-credential"
fi
if [[ -f "$app/Contents/Info.plist" ]]; then
  bundle_id=$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist")
  [[ "$bundle_id" == "$BUNDLE_ID" ]] || refuse "the app's bundle id is '$bundle_id', expected $BUNDLE_ID"
fi
# A profile made for another App ID signs, but macOS then refuses the
# entitlement or the app.
if [[ -n "$team" ]]; then
  app_id=$(plutil -extract Entitlements.com\\.apple\\.application-identifier raw -o - "$decoded" 2>/dev/null || true)
  expected_id="$team.${bundle_id:-$BUNDLE_ID}"
  [[ "$app_id" == "$expected_id" ]] ||
    refuse "the profile is for the App ID '$app_id', expected $expected_id (make one for ${bundle_id:-$BUNDLE_ID})"
fi
if [[ -n "$identity" && -n "$team" && "$identity" != *"($team)"* ]]; then
  refuse "the identity '$identity' is not of the profile's team $team"
fi

entitlements=$(mktemp)
sed -e "s/\${CHROMIUM_TEAM_ID}/$team/g" -e "s/\${CHROMIUM_BUNDLE_ID}/${bundle_id:-$BUNDLE_ID}/g" "$template" >"$entitlements"

steps=(
  "cp $profile $app/Contents/embedded.provisionprofile"
  "codesign --force --deep --timestamp=none --sign <identity> $app"
  "codesign --force --timestamp=none --sign <identity> --entitlements <entitlements> $app"
  "codesign --verify --deep --strict $app"
)
if [[ $dry -eq 1 ]]; then
  for step in "${steps[@]}"; do say "+ $step"; done
  [[ $problems -eq 0 ]] || exit 1
  exit 0
fi

cp "$profile" "$app/Contents/embedded.provisionprofile"
# Nested code first (helpers, the framework, libraries), then the app itself with the entitlements.
codesign --force --deep --timestamp=none --sign "$identity" "$app"
codesign --force --timestamp=none --sign "$identity" --entitlements "$entitlements" "$app"
codesign --verify --deep --strict "$app"
codesign -d --entitlements - "$app" 2>/dev/null | grep -q 'web-browser.public-key-credential' ||
  die "the signed app does not carry the passkey entitlement"
say "signed $app for team $team with the passkey entitlement"
