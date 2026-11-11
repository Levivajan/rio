#!/usr/bin/env bash
# Packages a built Rio.app into Rio.dmg with Sparkle updates (docs/chromium/m12.md, PLAN 2.3 and M12). It never
# builds: another stage makes out/Release, and this works from whatever .app it is given.
#
# Usage: package.sh [--app PATH] [--out DIR] [--dry-run]
#   --app      the built app (default: <checkout>/out/Release/Rio.app)
#   --out      where Rio.dmg, the update archive, appcast.xml and package.txt go (default: <repo>/out/package)
#   --dry-run  print the checks and the commands, change nothing
#
# Configuration comes from the environment, else from ~/.clu/package.env (KEY=VALUE lines; CLU_PACKAGE_ENV names
# another file). None of it lives in the repo, and none of it is a switch: what is configured is what happens.
#   CLU_BUNDLE_ID, CLU_SIGN_IDENTIFIER  the build's identity (scripts/chromium/common.sh); the app must carry the id
#   CLU_SIGN_IDENTITY    the signing identity: a self-signed one with CLU_SIGN_KEYCHAIN, else a "Developer ID
#                        Application: ..." identity. Unset: an ad-hoc signature.
#   CLU_SIGN_KEYCHAIN    the keychain holding the self-signed identity (~/.clu/signing/rio-signing.keychain-db, its
#                        password in keychain-password beside it): the same designated requirement for every build, no
#                        notarization (docs/chromium/m12.md section 7).
#   CLU_NOTARY_PROFILE   a notarytool keychain profile (xcrun notarytool store-credentials). Needs a Developer ID.
#   CLU_UPDATE_FEED_URL  the appcast the app checks: https, or http to 127.0.0.1 for the local update test. Unset:
#                        the package carries Sparkle but checks nothing.
#   CLU_UPDATE_KEY_FILE  the EdDSA key the update archives are signed with (default ~/.clu/sparkle/ed25519.key;
#                        make one with scripts/chromium/sparkle.mjs keygen). Needed with a feed.
#   CLU_UPDATE_BASE_URL  where the update archive will be served (default: the feed URL's directory).
#   CLU_APPCAST_FROM     the appcast published so far; this release is added to it (default: a new appcast).
# The default browser is never touched, and every app bundle this script copies is unregistered from LaunchServices
# at the end.
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

app=
out="$REPO/out/package"
dry=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) [[ $# -ge 2 && -n "$2" ]] || die "--app needs a path"; app=$2; shift ;;
    --out) [[ $# -ge 2 && -n "$2" ]] || die "--out needs a directory"; out=$2; shift ;;
    --dry-run) dry=1 ;;
    -h | --help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option '$1' (see --help)" ;;
  esac
  shift
done
app=${app:-$SRC/out/Release/Rio.app}
app=${app%/}

# ---- configuration --------------------------------------------------------------------------------------------------
KEYS=(CLU_SIGN_IDENTITY CLU_SIGN_KEYCHAIN CLU_NOTARY_PROFILE CLU_UPDATE_FEED_URL CLU_UPDATE_KEY_FILE CLU_UPDATE_BASE_URL CLU_APPCAST_FROM)
load_package_env "${KEYS[@]}"
identity=${CLU_SIGN_IDENTITY:-}
keychain=${CLU_SIGN_KEYCHAIN:-}
notary=${CLU_NOTARY_PROFILE:-}
feed=${CLU_UPDATE_FEED_URL:-}
key_file=${CLU_UPDATE_KEY_FILE:-$HOME/.clu/sparkle/ed25519.key}
base_url=${CLU_UPDATE_BASE_URL:-}
appcast_from=${CLU_APPCAST_FROM:-}
SPARKLE_JS="$REPO/scripts/chromium/sparkle.mjs"

problems=0
refuse() {
  if [[ $dry -eq 1 ]]; then
    say "would refuse: $*"
    problems=$((problems + 1))
  else
    die "$*"
  fi
}
run() {
  if [[ $dry -eq 1 ]]; then
    say "+ $*"
  else
    "$@"
  fi
}
plist() { plutil -extract "$1" raw -o - "$2" 2>/dev/null || true; }
VERSION_RE='^[0-9]{1,9}(\.[0-9]{1,9}){0,4}$'

# ---- the app --------------------------------------------------------------------------------------------------------
[[ -d "$app" ]] || die "no app at $app (build it with scripts/chromium/build.sh release, or pass --app)"
[[ "$(basename "$app")" == "Rio.app" ]] || die "the app must be named Rio.app, not $(basename "$app")"
info="$app/Contents/Info.plist"
[[ -f "$info" ]] || die "$app has no Contents/Info.plist"
bundle_id=$(plist CFBundleIdentifier "$info")
is_rio_id "$bundle_id" || die "bundle id '$bundle_id' is not this build's ($BUNDLE_ID, CLU_BUNDLE_ID)"
exe=$(plist CFBundleExecutable "$info")
[[ -n "$exe" && -x "$app/Contents/MacOS/$exe" ]] || die "$app has no executable Contents/MacOS/$exe"
archs=$(lipo -archs "$app/Contents/MacOS/$exe" 2>/dev/null || true)
[[ " $archs " == *" arm64 "* || " $archs " == *" arm64e "* ]] || die "Contents/MacOS/$exe is not an arm64 binary ($archs)"
version=$(plist CFBundleVersion "$info")
short=$(plist CFBundleShortVersionString "$info")
[[ "$version" =~ $VERSION_RE ]] || die "bad CFBundleVersion '$version'"
[[ "$short" =~ $VERSION_RE ]] || die "bad CFBundleShortVersionString '$short'"
min_system=$(plist LSMinimumSystemVersion "$info")
[[ "$min_system" =~ ^[0-9]+(\.[0-9]+){0,2}$ && ${min_system%%.*} -ge 12 ]] ||
  die "LSMinimumSystemVersion '$min_system' is below 12.0, which Sparkle 2 needs"

# What built it: args.gn sits beside the app in an out directory.
args_gn="$(dirname "$app")/args.gn"
build_kind="unknown (no args.gn beside the app)"
if [[ -f "$args_gn" ]]; then
  gn_true() { grep -Eq "^[[:space:]]*$1[[:space:]]*=[[:space:]]*$2" "$args_gn"; }
  ! gn_true is_component_build true ||
    die "$app is a component build: it loads its libraries from the out directory; package a release build"
  build_kind="not official"
  if gn_true is_official_build true; then
    build_kind="official"
    ! gn_true chrome_pgo_phase 2 || build_kind="official, PGO"
  fi
fi

# ---- signing and updates --------------------------------------------------------------------------------------------
[[ -z "$notary" || -n "$identity" ]] || refuse "CLU_NOTARY_PROFILE needs CLU_SIGN_IDENTITY: notarization takes a Developer ID signature"
packaging="$(dirname "$app")/Rio Packaging"
trap close_signing_keychain EXIT
if [[ -n "$keychain" ]]; then
  [[ -z "$notary" ]] || refuse "CLU_NOTARY_PROFILE with CLU_SIGN_KEYCHAIN: notarization takes a Developer ID signature, not a self-signed one"
  open_signing_keychain "$identity" "$keychain" || refuse "$SIGN_PROBLEM"
elif [[ -n "$identity" ]]; then
  [[ "$identity" == "Developer ID Application:"* ]] || refuse "CLU_SIGN_IDENTITY must be a Developer ID Application identity"
  [[ -f "$packaging/sign_chrome.py" ]] ||
    refuse "no '$packaging/sign_chrome.py': build the release with the chrome/installer/mac target"
  security find-identity -v -p codesigning 2>/dev/null | grep -Fq "\"$identity\"" ||
    refuse "the keychain has no valid identity \"$identity\""
fi
if [[ -n "$feed" ]]; then
  node --input-type=module -e "import { isAllowedFeedUrl } from '$SPARKLE_JS'; process.exit(isAllowedFeedUrl(process.argv[1]) ? 0 : 1)" "$feed" ||
    refuse "CLU_UPDATE_FEED_URL must be https, or http to 127.0.0.1, ::1 or localhost: $feed"
  public_key=$(node "$SPARKLE_JS" pubkey "$key_file" 2>&1) || refuse "the update key: ${public_key#sparkle.mjs: error: }"
  base_url=${base_url:-${feed%/*}}
  base_url=${base_url%/}
  [[ -z "$appcast_from" || -f "$appcast_from" ]] || refuse "CLU_APPCAST_FROM: no file at $appcast_from"
fi
if [[ $dry -eq 1 ]]; then
  sparkle=$("$REPO/scripts/chromium/fetch-sparkle.sh" --offline 2>/dev/null) || sparkle="(fetch-sparkle.sh downloads it)"
else
  sparkle=$("$REPO/scripts/chromium/fetch-sparkle.sh")
fi
sparkle_version=$(sed -n 's/^version=//p' "$CHROMIUM_DIR/sparkle.pin")

say "Rio $short ($version), $bundle_id, $archs, build: $build_kind"
if [[ -n "$keychain" ]]; then
  say "signature: $identity, self-signed from $keychain; no notarization (Gatekeeper asks before the first launch)"
elif [[ -n "$identity" ]]; then
  say "signature: $identity, through Chromium's sign_chrome.py; notarization: ${notary:-not configured}"
else
  say "signature: ad-hoc (no CLU_SIGN_IDENTITY; Gatekeeper on other Macs asks before the first launch)"
fi
if [[ -n "$feed" ]]; then
  say "updates: Sparkle $sparkle_version, feed $feed, archives at $base_url/"
else
  say "updates: Sparkle $sparkle_version embedded, no feed configured, so this package never checks"
fi

stage="$out/stage"
signed_root="$out/signed"
dmg_root="$out/dmg-root"
dmg="$out/Rio.dmg"
archive="$out/Rio-$short.zip"
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

# Copying an app makes LaunchServices list it; nothing this script made may stay listed as a browser on this Mac.
unregister_copies() {
  [[ -d "$out" ]] || return 0
  local real
  real=$(cd "$out" && pwd -P)
  "$LSREGISTER" -dump 2>/dev/null | sed -n 's/^path: *//p' | sed 's/ (0x[0-9a-f]*)$//' |
    grep -F -e "$out/" -e "$real/" | sort -u | while IFS= read -r path; do
      "$LSREGISTER" -u "$path" 2>/dev/null || true
    done || true
}

# codesign, quiet unless it fails ("replacing existing signature" is every call here).
adhoc_sign() {
  local log
  log=$(codesign --force --sign - --timestamp=none --preserve-metadata=entitlements "$1" 2>&1) || {
    printf '%s\n' "$log" >&2
    return 1
  }
}

sign_adhoc() {
  local bundle=$1 f
  # Loose Mach-O files first, then nested bundles deepest first, then the app: each seal covers signed code.
  while IFS= read -r -d '' f; do
    if file -b "$f" | grep -q 'Mach-O'; then
      adhoc_sign "$f"
    fi
  done < <(find "$bundle/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0)
  find "$bundle/Contents" -type d \( -name '*.app' -o -name '*.framework' -o -name '*.xpc' -o -name '*.appex' \
    -o -name '*.bundle' -o -name '*.plugin' -o -name '*.docktileplugin' \) |
    awk -F/ '{ print NF "\t" $0 }' | sort -rn -k1,1 | cut -f2- | while IFS= read -r f; do
      adhoc_sign "$f"
    done
  adhoc_sign "$bundle"
}

# Sparkle's own parts, inside out, as Sparkle's documentation signs them for a Developer ID app.
sign_sparkle_devid() {
  local fw="$1/Contents/Frameworks/Sparkle.framework"
  run codesign -f -s "$identity" -o runtime --timestamp "$fw/Versions/B/XPCServices/Installer.xpc"
  run codesign -f -s "$identity" -o runtime --timestamp --preserve-metadata=entitlements "$fw/Versions/B/XPCServices/Downloader.xpc"
  run codesign -f -s "$identity" -o runtime --timestamp "$fw/Versions/B/Autoupdate"
  run codesign -f -s "$identity" -o runtime --timestamp "$fw/Versions/B/Updater.app"
  run codesign -f -s "$identity" -o runtime --timestamp "$fw"
}

notarize() {
  local path=$1 result
  result=$(xcrun notarytool submit "$path" --keychain-profile "$notary" --wait --output-format json) ||
    die "notarytool failed for $path: $result"
  [[ "$result" == *'"status":"Accepted"'* ]] || die "notarization of $(basename "$path") was not accepted: $result"
}

if [[ $dry -eq 1 ]]; then
  say "+ ditto $app $stage/Rio.app; embed Sparkle.framework (arm64); write the update keys into Info.plist"
  if [[ -n "$keychain" ]]; then
    say "+ codesign --force --sign '$identity' --keychain $keychain --timestamp=none (every Mach-O file and bundle, inside out; Chromium's entitlements for the app and the renderer and GPU helpers)"
  elif [[ -n "$identity" ]]; then
    sign_sparkle_devid "$stage/Rio.app"
    say "+ python3 '$packaging/sign_chrome.py' --input $stage --output $signed_root --identity '$identity' --disable-packaging --notarize none"
    [[ -z "$notary" ]] || say "+ xcrun notarytool submit Rio.zip --keychain-profile '$notary' --wait; xcrun stapler staple Rio.app"
  else
    say "+ codesign --force --sign - (every Mach-O file and bundle, inside out)"
  fi
  say "+ hdiutil create -volname Rio -srcfolder $dmg_root -fs HFS+ -format UDZO $dmg; hdiutil verify $dmg"
  if [[ -n "$keychain" ]]; then
    say "+ codesign --sign '$identity' --keychain $keychain --timestamp=none $dmg"
  elif [[ -n "$identity" ]]; then
    say "+ codesign --sign '$identity' --timestamp $dmg"
  fi
  [[ -z "$notary" ]] || say "+ xcrun notarytool submit $dmg --keychain-profile '$notary' --wait; xcrun stapler staple $dmg"
  [[ -z "$feed" ]] || say "+ ditto -c -k --keepParent Rio.app $archive; sparkle.mjs appcast --out $out/appcast.xml --url $base_url/$(basename "$archive")"
  say "+ $LSREGISTER -u <every app copied under $out>"
  [[ $problems -eq 0 ]] || exit 1
  exit 0
fi

mkdir -p "$out"
rm -rf "$stage" "$signed_root" "$dmg_root" "$dmg" "$archive" "$out/appcast.xml" "$out/package.txt"
trap 'unregister_copies; rm -rf "$stage" "$signed_root" "$dmg_root"; close_signing_keychain' EXIT
mkdir -p "$stage"
ditto "$app" "$stage/Rio.app"
staged="$stage/Rio.app"

embed_sparkle "$staged" "$sparkle" "$feed" "${public_key:-}"

if [[ -n "$keychain" ]]; then
  sign_self_signed "$staged"
  final=$staged
elif [[ -n "$identity" ]]; then
  sign_sparkle_devid "$staged"
  ditto "$packaging" "$stage/Rio Packaging"
  mkdir -p "$signed_root"
  python3 "$stage/Rio Packaging/sign_chrome.py" --input "$stage" --output "$signed_root" --identity "$identity" \
    --disable-packaging --notarize none
  final=$(find "$signed_root" -maxdepth 3 -type d -name Rio.app | head -n 1)
  [[ -n "$final" ]] || die "sign_chrome.py left no Rio.app under $signed_root"
else
  sign_adhoc "$staged"
  final=$staged
fi
codesign --verify --deep --strict "$final" || die "the signed app does not verify"
signature=$(codesign -dvv "$final" 2>&1 | sed -n 's/^Signature=//p; s/^Authority=//p' | head -n 1)
requirement=$(codesign -d -r- "$final" 2>&1 | sed -n 's/^designated => //p')

notarized=no
if [[ -n "$notary" ]]; then
  ditto -c -k --keepParent "$final" "$out/notarize-app.zip"
  notarize "$out/notarize-app.zip"
  rm -f "$out/notarize-app.zip"
  xcrun stapler staple "$final"
  notarized=yes
fi

# Widevine: the release build writes the VMP signature next to the framework when Google's cert is set up.
widevine=$(find "$final/Contents/Frameworks" -maxdepth 5 -path '*/Resources/*.sig' -type f 2>/dev/null | wc -l | tr -d ' ')

mkdir -p "$dmg_root"
ditto "$final" "$dmg_root/Rio.app"
ln -s /Applications "$dmg_root/Applications"
hdiutil create -quiet -volname Rio -srcfolder "$dmg_root" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$dmg"
hdiutil verify -quiet "$dmg"
if [[ -n "$keychain" ]]; then
  self_sign "$dmg"
elif [[ -n "$identity" ]]; then
  codesign --sign "$identity" --timestamp "$dmg"
fi
if [[ -n "$notary" ]]; then
  notarize "$dmg"
  xcrun stapler staple "$dmg"
fi
gatekeeper=$(spctl -a -vv -t install "$dmg" 2>&1 | head -n 1 || true)

if [[ -n "$feed" ]]; then
  ditto -c -k --sequesterRsrc --keepParent "$final" "$archive"
  appcast_args=(appcast --out "$out/appcast.xml" --archive "$archive" --url "$base_url/$(basename "$archive")"
    --version "$version" --short "$short" --min-system "$min_system" --key "$key_file")
  [[ -z "$appcast_from" ]] || appcast_args+=(--from "$appcast_from")
  node "$SPARKLE_JS" "${appcast_args[@]}" >/dev/null
fi

{
  printf 'Rio %s (%s)\n' "$short" "$version"
  printf 'bundle id: %s\narchitectures: %s\nbuild: %s\n' "$bundle_id" "$archs" "$build_kind"
  printf 'signature: %s\ndesignated requirement: %s\n' "${signature:-unknown}" "${requirement:-unknown}"
  printf 'notarized: %s\ngatekeeper: %s\n' "$notarized" "$gatekeeper"
  printf 'widevine signatures: %s\n' "$widevine"
  printf 'sparkle: %s\nfeed: %s\n' "$sparkle_version" "${feed:-none}"
  printf 'dmg: %s\n' "$(basename "$dmg")"
  [[ -z "$feed" ]] || printf 'update archive: %s\nappcast: appcast.xml\n' "$(basename "$archive")"
} >"$out/package.txt"
say "wrote $dmg"
sed 's/^/  /' "$out/package.txt"
