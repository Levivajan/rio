#!/usr/bin/env bash
# Publishes what package.sh wrote (docs/chromium/m12.md section 9) as a GitHub release of the repository CLU_RELEASES_REPO
# (owner/name, from the environment or ~/.clu/package.env; no default), whose latest release serves the feed:
#   https://github.com/<CLU_RELEASES_REPO>/releases/latest/download/appcast.xml
# The release's assets are Rio-<version>.zip (Sparkle's signed update), appcast.xml and Rio.dmg (first installs). The
# appcast names the zip under .../releases/latest/download/, so the newest release must hold both.
#
# Usage: publish-release.sh [--out DIR] [--publish]
#   --out      what package.sh wrote (default: <repo>/out/package)
#   --publish  create the release; without it, print the checks and the command and change nothing
# Needs `gh` signed in to an account that can write CLU_RELEASES_REPO. Run it only to publish a release.
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

REPO_SLUG=${CLU_RELEASES_REPO:-}
out="$REPO/out/package"
publish=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) [[ $# -ge 2 && -n "$2" ]] || die "--out needs a directory"; out=$2; shift ;;
    --publish) publish=1 ;;
    -h | --help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option '$1' (see --help)" ;;
  esac
  shift
done

[[ "$REPO_SLUG" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] ||
  die "set CLU_RELEASES_REPO to the releases repository (owner/name), in the environment or ~/.clu/package.env"
[[ -f "$out/package.txt" ]] || die "no $out/package.txt: run scripts/chromium/package.sh first"
feed=$(sed -n 's/^feed: //p' "$out/package.txt")
[[ "$feed" == "https://github.com/$REPO_SLUG/releases/latest/download/appcast.xml" ]] ||
  die "the package's feed is '$feed', not $REPO_SLUG's latest release: set CLU_UPDATE_FEED_URL and package again"
archive=$(sed -n 's/^update archive: //p' "$out/package.txt")
short=$(sed -n '1s/^Rio \([^ ]*\) .*/\1/p' "$out/package.txt")
for f in "$archive" appcast.xml Rio.dmg; do
  [[ -n "$f" && -f "$out/$f" ]] || die "missing $out/$f"
done
grep -Fq "$archive" "$out/appcast.xml" || die "appcast.xml does not name $archive"
grep -q 'sparkle:edSignature=' "$out/appcast.xml" || die "appcast.xml has no EdDSA signature"

cmd=(gh release create "v$short" --repo "$REPO_SLUG" --title "Rio $short" --notes "Rio $short"
  "$out/$archive" "$out/appcast.xml" "$out/Rio.dmg")
if [[ $publish -eq 0 ]]; then
  say "would run: ${cmd[*]}"
  say "nothing published (pass --publish)"
  exit 0
fi
"${cmd[@]}"
say "published Rio $short to $REPO_SLUG"
