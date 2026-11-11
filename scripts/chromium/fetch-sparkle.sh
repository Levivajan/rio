#!/usr/bin/env bash
# Downloads the Sparkle release pinned in chromium/sparkle.pin into out/sparkle/<version> and checks it against the
# pinned SHA-256 before anything uses it (docs/chromium/m12.md). Prints the extracted directory (it holds
# Sparkle.framework). A verified copy is reused; a copy that fails the check is deleted.
#
# Usage: fetch-sparkle.sh [--offline]   --offline: use the verified copy only, never the network
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

offline=0
case "${1:-}" in
  --offline) offline=1 ;;
  '') ;;
  -h | --help) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) die "unknown option '$1' (see --help)" ;;
esac

pin="$CHROMIUM_DIR/sparkle.pin"
pin_value() {
  local v
  v=$(sed -n "s/^$1=//p" "$pin")
  [[ -n "$v" ]] || die "chromium/sparkle.pin has no $1="
  printf '%s\n' "$v"
}
version=$(pin_value version)
url=$(pin_value url)
sha=$(pin_value sha256)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "chromium/sparkle.pin: bad version '$version'"
[[ "$url" == "https://github.com/sparkle-project/Sparkle/releases/download/$version/Sparkle-$version.tar.xz" ]] ||
  die "chromium/sparkle.pin: the url must be Sparkle's own GitHub release asset for $version"
[[ "$sha" =~ ^[0-9a-f]{64}$ ]] || die "chromium/sparkle.pin: bad sha256"

dir="$REPO/out/sparkle/$version"
dist="$dir/dist"
if [[ -f "$dir/.verified" && "$(cat "$dir/.verified")" == "$sha" && -d "$dist/Sparkle.framework" ]]; then
  printf '%s\n' "$dist"
  exit 0
fi
[[ $offline -eq 0 ]] || die "no verified Sparkle $version in $dir; run scripts/chromium/fetch-sparkle.sh"

# A fresh, empty directory for the download: nothing from an earlier try is trusted.
rm -rf "$dir"
mkdir -p "$dir"
archive="$dir/Sparkle-$version.tar.xz"
say "downloading Sparkle $version from GitHub" >&2
curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 --max-time 300 -o "$archive" "$url"
got=$(shasum -a 256 "$archive" | awk '{print $1}')
if [[ "$got" != "$sha" ]]; then
  rm -rf "$dir"
  die "Sparkle $version has SHA-256 $got, the pin says $sha; refusing it"
fi
mkdir "$dist"
tar -xJf "$archive" -C "$dist" ./Sparkle.framework ./bin ./LICENSE
[[ -d "$dist/Sparkle.framework/Versions/B" ]] || die "Sparkle $version has no Sparkle.framework/Versions/B"
printf '%s\n' "$sha" >"$dir/.verified"
say "Sparkle $version verified ($sha)" >&2
printf '%s\n' "$dist"
