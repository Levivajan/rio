#!/usr/bin/env bash
# Applies Clu to the Chromium checkout (docs/chromium/PLAN.md 2.3): every chromium/patches/*.patch in name order, the
# branding files from chromium/branding/clu (copied over their upstream paths), the new code from chromium/clu into
# src/clu, then scripts/chromium/rebrand-strings.py. Records every file it touched, with its hash, in src/.clu-applied.
#
# Usage: apply.sh             apply (a rerun reverts the previous apply first, so it is idempotent; files whose bytes
#                             end up the same keep their mtime, so the next build redoes only what changed)
#        apply.sh --dry-run   check every patch against HEAD and report what would change; writes nothing
#        apply.sh --revert    put every recorded file back to HEAD and remove the copied ones
#        apply.sh --revert --force   the same, even over files edited by hand after the apply
#        apply.sh --stats     patched upstream files and lines against the budget (60 files, 1,500 lines)
#
# It refuses to touch a tree with changes it did not make, and refuses while a build owns the checkout.
# Env: CLU_CHROMIUM_ROOT (gclient root, default ~/Developer/clu-chromium.noindex), CLU_CHROMIUM_SRC (default ROOT/src).
set -euo pipefail
# shellcheck source=scripts/chromium/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

MAX_FILES=60
MAX_LINES=1500
mode=apply
force=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) mode=dry-run ;;
    --revert) mode=revert ;;
    --stats) mode=stats ;;
    --force) force=1 ;;
    -h | --help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument '$arg' (see --help)" ;;
  esac
done
[[ $force -eq 0 || $mode == revert ]] || die "--force only goes with --revert"

shopt -s nullglob
PATCHES=("$CHROMIUM_DIR"/patches/*.patch)
shopt -u nullglob

# Upstream paths a patch writes (its "+++ b/" lines).
patch_targets() {
  sed -n 's|^+++ b/||p' "$1"
}

if [[ $mode == stats ]]; then
  files=$(for p in ${PATCHES[@]+"${PATCHES[@]}"}; do patch_targets "$p"; done | sort -u | grep -c . || true)
  lines=$(cat /dev/null ${PATCHES[@]+"${PATCHES[@]}"} | grep -E '^[+-]' | grep -cvE '^(\+\+\+|---) ' || true)
  say "${#PATCHES[@]} patches, $files patched upstream files (budget $MAX_FILES), $lines patched lines (budget $MAX_LINES)"
  [[ $files -le $MAX_FILES && $lines -le $MAX_LINES ]] || die "over the patch budget (PLAN 2.2)"
  exit 0
fi

[[ -d "$SRC" ]] || die "no Chromium checkout at $SRC (run sync.sh, or set CLU_CHROMIUM_SRC)"
SRC=$(cd "$SRC" && pwd -P)
[[ "$(git -C "$SRC" rev-parse --show-toplevel 2>/dev/null)" == "$SRC" ]] || die "$SRC is not the top of a git checkout"
MANIFEST="$SRC/$MANIFEST_NAME"
export GIT_OPTIONAL_LOCKS=0

sha() {
  shasum -a 256 "$1" | cut -d' ' -f1
}

# Manifest rows: kind <TAB> sha256 <TAB> path. kind is "tracked" (an upstream file, restored from HEAD) or "new"
# (a file Clu added, deleted on revert). Lines starting with # are notes. bash 3.2 (macOS) has no associative
# arrays, so lookups go through awk.
ROWS=$(mktemp -t clu-apply)
TOUCHED=$(mktemp -t clu-apply)
STAMPS=$(mktemp -d -t clu-apply)
trap 'rm -rf "$ROWS" "$TOUCHED" "$STAMPS"' EXIT
if [[ -e "$MANIFEST" ]]; then
  bad=$(awk -F'\t' '!/^#/ && NF && !(($1 == "tracked" || $1 == "new") && $2 ~ /^[0-9a-f]+$/ && length($2) == 64 &&
    NF == 3 && $3 != "" && $3 !~ /^\// && $3 !~ /\.\./) { print NR; exit }' "$MANIFEST")
  [[ -z "$bad" ]] || die "$MANIFEST line $bad is corrupt; restore the tree by hand (git -C $SRC status) and delete the manifest"
  grep -v '^#' "$MANIFEST" | grep . >"$ROWS" || true
fi
recorded_count() {
  grep -c . "$ROWS" || true
}
recorded_hash() {
  awk -F'\t' -v p="$1" '$3 == p { print $2 }' "$ROWS"
}

# Files that differ from HEAD (tracked changes and untracked files), minus the manifest itself.
dirty_paths() {
  local entry
  git -C "$SRC" status --porcelain=v1 -z --untracked-files=all | while IFS= read -r -d '' entry; do
    [[ "${entry:3}" == "$MANIFEST_NAME" ]] || printf '%s\n' "${entry:3}"
  done
}

# Dirty files the manifest doesn't explain, or recorded files edited since the apply.
foreign_changes() {
  local path hash
  while IFS= read -r path; do
    [[ -n "$path" ]] || continue
    hash=$(recorded_hash "$path")
    if [[ -z "$hash" ]]; then
      printf '  %s (not made by apply.sh)\n' "$path"
    elif [[ -f "$SRC/$path" && "$(sha "$SRC/$path")" != "$hash" ]]; then
      printf '  %s (edited after apply.sh; save it with a patch first)\n' "$path"
    fi
  done < <(dirty_paths)
}

revert_recorded() {
  local kind hash path
  while IFS=$'\t' read -r kind hash path; do
    if [[ "$kind" == tracked ]]; then
      git -C "$SRC" checkout -q HEAD -- "$path"
    else
      rm -f "$SRC/$path"
    fi
  done <"$ROWS"
  [[ ! -d "$SRC/clu" ]] || find "$SRC/clu" -depth -type d -empty -delete
  rm -f "$MANIFEST"
  : >"$ROWS"
}

if [[ $mode == revert ]]; then
  [[ -e "$MANIFEST" ]] || { say "nothing to revert: no $MANIFEST_NAME in $SRC"; exit 0; }
  assert_no_build
  if [[ $force -eq 0 ]]; then
    bad=$(foreign_changes | grep 'edited after' || true)
    [[ -z "$bad" ]] || die "these files changed after the apply; rerun with --force to drop the edits:
$bad"
  fi
  count=$(recorded_count)
  revert_recorded
  say "reverted $count files in $SRC"
  exit 0
fi

if [[ $mode == apply ]]; then
  assert_no_build
fi
foreign=$(foreign_changes)
[[ -z "$foreign" ]] || die "$SRC has changes apply.sh did not make, so it won't touch it:
$foreign"

if [[ $mode == dry-run ]]; then
  failed=0
  [[ ! -e "$MANIFEST" ]] || say "would revert the previous apply ($(recorded_count) files) first"
  for p in ${PATCHES[@]+"${PATCHES[@]}"}; do
    # --cached checks against HEAD (the index), whatever an earlier apply left in the working tree.
    if out=$(git -C "$SRC" apply --check --cached "$p" 2>&1); then
      say "ok      ${p##*/}"
    else
      say "FAILED  ${p##*/}: $out"
      failed=$((failed + 1))
    fi
  done
  overlay=$(find "$CHROMIUM_DIR/branding/clu" -type f 2>/dev/null | grep -c . || true)
  code=$(find "$CHROMIUM_DIR/clu" -type f 2>/dev/null | grep -c . || true)
  say "would copy $overlay branding files and $code files into src/clu"
  python3 -B "$REPO/scripts/chromium/rebrand-strings.py" --src "$SRC" --dry-run >/dev/null
  [[ $failed -eq 0 ]] || die "$failed of ${#PATCHES[@]} patches would fail"
  say "dry run passed: ${#PATCHES[@]} patches apply cleanly"
  exit 0
fi

# Before a re-apply reverts anything: each recorded file's hash, and an empty stand-in under $STAMPS that carries its
# mtime. restore_stamps gives every file the apply writes back byte for byte its old mtime, so ninja and grit see
# only the files that really changed.
save_stamps() {
  local kind hash path
  while IFS=$'\t' read -r kind hash path; do
    [[ -f "$SRC/$path" ]] || continue
    mkdir -p "$(dirname "$STAMPS/t/$path")"
    touch -r "$SRC/$path" "$STAMPS/t/$path"
    printf '%s\t%s\n' "$(sha "$SRC/$path")" "$path"
  done <"$ROWS" >"$STAMPS/rows"
}
restore_stamps() {
  local path kept=0
  [[ -f "$STAMPS/rows" ]] || return 0
  while IFS= read -r path; do
    touch -r "$STAMPS/t/$path" "$SRC/$path"
    kept=$((kept + 1))
  done < <(awk -F'\t' 'NR == FNR { old[$2] = $1; next } !/^#/ && NF == 3 && old[$3] == $2 { print $3 }' \
    "$STAMPS/rows" "$MANIFEST")
  say "kept the mtime of $kept files whose bytes did not change"
}

# apply
if [[ -e "$MANIFEST" ]]; then
  say "reverting the previous apply ($(recorded_count) files)"
  save_stamps
  revert_recorded
fi
[[ ! -e "$SRC/clu" ]] || die "$SRC/clu exists but no manifest records it; move it away first"

touch_file() { # kind path
  printf '%s\t%s\n' "$1" "$2" >>"$TOUCHED"
}
notes=()
failed=()
for p in ${PATCHES[@]+"${PATCHES[@]}"}; do
  name=${p##*/}
  targets=$(patch_targets "$p")
  [[ -n "$targets" ]] || { failed+=("$name: no '+++ b/' target in the patch"); continue; }
  if git -C "$SRC" apply --check "$p" 2>/dev/null; then
    git -C "$SRC" apply "$p"
  elif out=$(git -C "$SRC" apply --3way "$p" 2>&1); then
    say "applied ${name} with a 3-way merge"
  else
    # --3way leaves conflict markers to fix by hand; the files are still recorded so --revert cleans them.
    failed+=("$name: $out")
  fi
  while IFS= read -r t; do touch_file tracked "$t"; done <<<"$targets"
  notes+=("# patch $(sha "$p") $name")
done

copy_tree() { # from-dir to-prefix
  local f rel dest
  [[ -d "$1" ]] || return 0
  while IFS= read -r -d '' f; do
    rel=${f#"$1"/}
    dest="$2$rel"
    if git -C "$SRC" ls-files --error-unmatch -- "$dest" >/dev/null 2>&1; then
      touch_file tracked "$dest"
    else
      touch_file new "$dest"
    fi
    mkdir -p "$(dirname "$SRC/$dest")"
    cp "$f" "$SRC/$dest"
  done < <(find "$1" -type f -print0)
}
copy_tree "$CHROMIUM_DIR/branding/clu" ""
copy_tree "$CHROMIUM_DIR/clu" "clu/"

if strings=$(python3 -B "$REPO/scripts/chromium/rebrand-strings.py" --src "$SRC"); then
  while IFS= read -r t; do [[ -z "$t" ]] || touch_file tracked "$t"; done <<<"$strings"
else
  failed+=("rebrand-strings.py failed")
fi

{
  printf '# Written by scripts/chromium/apply.sh at %s for Chromium %s. Undo with apply.sh --revert.\n' \
    "$(date -u +%FT%TZ)" "$(pinned_version)"
  [[ ${#notes[@]} -eq 0 ]] || printf '%s\n' "${notes[@]}"
  # One row per path, the first kind seen (a patched file stays "tracked" even if a later step rewrites it).
  awk -F'\t' '!seen[$2]++' "$TOUCHED" | sort -t$'\t' -k2 | while IFS=$'\t' read -r kind path; do
    [[ ! -f "$SRC/$path" ]] || printf '%s\t%s\t%s\n' "$kind" "$(sha "$SRC/$path")" "$path"
  done
} >"$MANIFEST"
restore_stamps

say "${#PATCHES[@]} patches, $(awk -F'\t' '!seen[$2]++' "$TOUCHED" | grep -c .) files touched, ${#failed[@]} failures"
"$0" --stats
if [[ ${#failed[@]} -gt 0 ]]; then
  printf '  %s\n' "${failed[@]}" >&2
  die "${#failed[@]} steps failed; fix them, or undo with apply.sh --revert --force"
fi
