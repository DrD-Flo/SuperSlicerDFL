#!/usr/bin/env bash
#
# Sync the BUNDLED DFL-Printers seed (the resources/profiles submodule) to the latest profile from the
# dedicated update repo, so a FRESH SuperSlicer DFL install ships the current version instead of an old
# baseline. Copies the .ini/.idx/icons, commits the submodule, and bumps the gitlink. Rebuild afterwards
# to actually ship it.
#
# This is SEPARATE from DFL-Printers-Profile/publish.sh:
#   - publish.sh   -> updates the ONLINE feed every release (no rebuild).
#   - sync-dfl-seed.sh -> refreshes what FRESH INSTALLS start at (needs a rebuild). Run it occasionally,
#     typically when cutting a new app build.
#
# Usage:
#   ./sync-dfl-seed.sh [--from <DFL-Printers-Profile dir>] [--repo <owner/repo>] [--no-push] [--no-commit]
#
#   ./sync-dfl-seed.sh                                       # copy latest seed, commit submodule + gitlink, push
#   ./sync-dfl-seed.sh --repo DrD-Flo/DFL-Printers-Profile   # production: point the seed's update key at DrD-Flo
#   ./sync-dfl-seed.sh --no-push                             # commit locally, don't push
#   ./sync-dfl-seed.sh --no-commit                           # just copy the files for review
#
set -euo pipefail
SUPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUB="$SUPER_DIR/resources/profiles"
FROM="$SUPER_DIR/../DFL-Printers-Profile"
REPO=""
PUSH=1
COMMIT=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from) FROM="${2:?--from needs a path}"; shift 2;;
    --repo) REPO="${2:?--repo needs owner/repo}"; shift 2;;
    --no-push) PUSH=0; shift;;
    --no-commit) COMMIT=0; shift;;
    -h|--help) sed -n '2,18p' "$0"; exit 0;;
    *) echo "Unknown arg: $1" >&2; exit 1;;
  esac
done

SRC="$FROM/profiles"
[[ -f "$SRC/DFL-Printers.ini" ]] || { echo "ERROR: $SRC/DFL-Printers.ini not found (pass --from <DFL-Printers-Profile dir>)" >&2; exit 1; }

# 1) copy ini + idx + icons into the submodule
cp "$SRC/DFL-Printers.ini" "$SUB/DFL-Printers.ini"
cp "$SRC/DFL-Printers.idx" "$SUB/DFL-Printers.idx"
mkdir -p "$SUB/DFL-Printers"
cp "$SRC/DFL-Printers/"*.png "$SUB/DFL-Printers/" 2>/dev/null || true

# 2) optionally point the seed's update key at a specific repo (e.g. production DrD-Flo)
if [[ -n "$REPO" ]]; then
  tmp="$(mktemp)"
  awk -v r="$REPO" '/^config_update_github *=/{print "config_update_github = " r; next}{print}' "$SUB/DFL-Printers.ini" > "$tmp" && mv "$tmp" "$SUB/DFL-Printers.ini"
fi

SEED_VER=$(awk -F= '/^config_version/{gsub(/ /,"");print $2;exit}' "$SUB/DFL-Printers.ini")
KEY=$(awk -F= '/^config_update_github/{gsub(/ /,"");print $2;exit}' "$SUB/DFL-Printers.ini")
echo "Bundled seed -> config_version=$SEED_VER  config_update_github=$KEY"

if [[ "$COMMIT" == 0 ]]; then
  echo "Files copied into $SUB (no commit). Review, then commit the submodule + bump the gitlink."
  exit 0
fi

# 3) commit submodule (only if it actually changed)
git -C "$SUB" add DFL-Printers.ini DFL-Printers.idx DFL-Printers/
if git -C "$SUB" diff --cached --quiet; then
  echo "Seed already at $SEED_VER — nothing to commit."
  exit 0
fi
git -C "$SUB" commit -q -m "DFL-Printers: bump bundled seed to $SEED_VER"
if [[ "$PUSH" == 1 ]]; then git -C "$SUB" push -q && echo "pushed submodule"; fi

# 4) bump the gitlink in the superproject (only the submodule pointer)
git -C "$SUPER_DIR" add resources/profiles
git -C "$SUPER_DIR" commit -q -m "Update profiles submodule: bundled DFL-Printers seed -> $SEED_VER"
if [[ "$PUSH" == 1 ]]; then git -C "$SUPER_DIR" push -q && echo "pushed superproject"; fi

echo "Done. Rebuild the app (raw + DMG) so fresh installs ship $SEED_VER."
