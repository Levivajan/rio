#!/usr/bin/env bash
# Checks a built Rio.app against the branding (docs/chromium/PLAN.md 2.2 and section 6, M1; the product name is Rio and
# the code name clu, docs/chromium/rio-rename.md): bundle id CLU_BUNDLE_ID, name Rio, the icon generated from
# assets/brand/clu-icon.svg, the data dir ~/Library/Application Support/Rio, the "Rio Safe Storage"
# keychain item, and no user-visible "Chromium", "Google Chrome", "Clu", forbidden word or banned name in the plists or
# the locale .pak files.
#
# Usage: check-brand.sh [APP]   (default: <src>/out/Dev/Rio.app)
# Env: CLU_CHROMIUM_ROOT (gclient root, default ~/Developer/clu-chromium.noindex), CLU_CHROMIUM_SRC (default ROOT/src),
# CLU_BRAND_WORDS (forbidden words, comma-separated; else ~/.clu/brand-words.txt, see brand_scan.py).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

case "${1:-}" in
  -h | --help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
[[ $# -le 1 ]] || die "takes at most one argument, the app (see --help)"
app=${1:-$SRC/out/Dev/Rio.app}
[[ -d "$app/Contents" ]] || die "no app bundle at $app (build it with build.sh dev)"
app=$(cd "$app" && pwd -P)
branding="$CHROMIUM_DIR/branding/clu/chrome/app/theme/chromium/mac"

failures=0
pass() { printf 'PASS  %s\n' "$*"; }
fail() {
  printf 'FAIL  %s\n' "$*"
  failures=$((failures + 1))
}
plist_value() { # key: the main Info.plist's value, empty when missing
  plutil -extract "$1" raw -o - "$app/Contents/Info.plist" 2>/dev/null || true
}
expect_plist() { # key expected
  local got
  got=$(plist_value "$1")
  if [[ "$got" == "$2" ]]; then pass "$1 is $2"; else fail "$1 is '${got}', expected '$2'"; fi
}

if [[ "${app##*/}" == Rio.app ]]; then pass "bundle is Rio.app"; else fail "bundle is ${app##*/}, expected Rio.app"; fi
expect_plist CFBundleIdentifier "$BUNDLE_ID"
expect_plist CFBundleName Rio
expect_plist CFBundleDisplayName Rio
expect_plist CFBundleExecutable Rio
expect_plist CrProductDirName Rio
[[ "$(plist_value CrProductDirName)" != Rio ]] ||
  pass "user data goes to ~/Library/Application Support/Rio"

want=$(cut -d' ' -f1 "$CHROMIUM_DIR/branding/clu-icon.sha256")
have=$(shasum -a 256 "$REPO/assets/brand/clu-icon.svg" | cut -d' ' -f1)
if [[ "$want" == "$have" ]]; then
  pass "chromium/branding matches assets/brand/clu-icon.svg"
else
  fail "assets/brand/clu-icon.svg changed since chromium/branding was generated; run node scripts/chromium/make-branding.mjs"
fi
for f in app.icns Assets.car; do
  if cmp -s "$branding/$f" "$app/Contents/Resources/$f"; then
    pass "Contents/Resources/$f is Rio's"
  else
    fail "Contents/Resources/$f differs from chromium/branding (was apply.sh run before the build?)"
  fi
done

# The keychain service name is a compiled-in string (components/os_crypt/common/keychain_password_mac.mm). In a
# component build the framework binary only re-exports @rpath/lib*.dylib files that stay beside the app (out/Dev),
# so search those too.
code=("$app/Contents")
if otool -L "$app/Contents/Frameworks/Rio Framework.framework/Rio Framework" 2>/dev/null | grep -q '@rpath/libchrome_dll\.dylib'; then
  shopt -s nullglob
  code+=("$(dirname "$app")"/*.dylib)
  shopt -u nullglob
fi
if LC_ALL=C grep -rqaF 'Rio Safe Storage' "${code[@]}"; then
  pass "keychain item is \"Rio Safe Storage\""
else
  fail "no \"Rio Safe Storage\" string in the bundle"
fi
if LC_ALL=C grep -rqaF 'Chromium Safe Storage' "${code[@]}"; then
  fail "the bundle still contains \"Chromium Safe Storage\""
fi
if LC_ALL=C grep -rqaF 'Clu Safe Storage' "${code[@]}"; then
  fail "the bundle still contains the code-name item \"Clu Safe Storage\""
fi

if python3 -B "$REPO/scripts/chromium/brand_scan.py" "$app"; then
  pass "no user-visible Chromium, Google Chrome, Clu, forbidden word or banned name in the plists and locale .pak files"
else
  fail "user-visible upstream, code or banned names (listed above)"
fi

[[ $failures -eq 0 ]] || die "$failures branding checks failed for $app"
say "all branding checks passed for $app"
