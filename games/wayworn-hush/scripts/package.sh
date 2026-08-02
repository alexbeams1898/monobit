#!/usr/bin/env bash
# package.sh -- create a shareable zip of wayworn-hush.
#
# Usage:
#   ./games/wayworn-hush/scripts/package.sh [output-name]
#
# Bundles the game exe + assets + config into a zip ready to hand to someone.
# Output name defaults to "wayworn-hush-v<VERSION>" (read from this game's
# CMakeLists.txt project() declaration). Creates <name>.zip in the repo root.
#
# This builds its OWN exe into a separate release build dir rather than shipping
# the one F7 makes. A dev build hard-codes an absolute chdir to THIS machine's
# source tree (WAYWORN_SOURCE_DIR) so asset edits are live without a copy step --
# on anyone else's machine that path doesn't exist, the chdir fails, and the game
# finds no assets at all. WAYWORN_RELEASE=ON omits the chdir so the exe reads
# assets sitting beside it, which is what a shared copy needs.
#
# Modeled on games/prison-escape-game/scripts/package.sh -- notably its ldd sweep,
# which finds every non-system DLL the exe actually needs instead of keeping a
# hand-written list that silently rots.

set -e

# Resolve script dir / game dir / repo root from this script's location, so
# package.sh can be invoked from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(cd "$GAME_DIR/../.." && pwd)"

cd "$REPO_ROOT"

EXE_NAME="wayworn-hush.exe"
BUILD_DIR="build-release"          # kept apart from build/ so F7 stays a dev build
BUILD_BIN="$BUILD_DIR/bin/wayworn-hush"

# The version off this game's project() line. sed, not grep -P: this box's locale
# makes -P refuse to run at all, and the usual `|| echo unknown` fallback turns that
# into a silently mis-named zip rather than an error anyone notices.
VERSION=$(sed -n 's/.*project(.*VERSION[[:space:]]\+\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p' \
    "$GAME_DIR/CMakeLists.txt" | head -1)
if [ -z "$VERSION" ]; then
    echo "ERROR: no VERSION found in $GAME_DIR/CMakeLists.txt project() line." >&2
    exit 1
fi
NAME="${1:-wayworn-hush-v$VERSION}"
STAGE_DIR="$NAME"

# --- build a release exe ------------------------------------------------------
echo "Configuring a release build (assets read from beside the exe)..."
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DWAYWORN_RELEASE=ON \
    >/dev/null

echo "Building $EXE_NAME..."
cmake --build "$BUILD_DIR" --target wayworn-hush >/dev/null

if [ ! -f "$BUILD_BIN/$EXE_NAME" ]; then
    echo "ERROR: $BUILD_BIN/$EXE_NAME not found after the build." >&2
    exit 1
fi

# --- stage ---------------------------------------------------------------------
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp "$BUILD_BIN/$EXE_NAME" "$STAGE_DIR/$EXE_NAME"

# Assets + config come from the SOURCE tree: this game has no copy-to-build step
# (that's the point of the dev chdir), so the source tree is the only place they
# exist. This is where a shared copy gets its own snapshot of them.
cp -r "$GAME_DIR/assets" "$STAGE_DIR/assets"
cp -r "$GAME_DIR/config" "$STAGE_DIR/config"

# The .ldtk source file and its backups are authoring data -- the game reads the
# .ldtk itself, so it stays, but the editor's backup folder is noise to ship.
rm -rf "$STAGE_DIR/assets/tilesets/source/backups"

# --- DLLs ----------------------------------------------------------------------
# Find every dependency the exe actually has and take the non-system ones. No
# hand-maintained list to fall out of date when a dependency changes.
echo "Scanning DLL dependencies..."
dll_count=0
while IFS= read -r dll_path; do
    [ -z "$dll_path" ] && continue
    dll_name=$(basename "$dll_path")
    cp "$dll_path" "$STAGE_DIR/$dll_name"
    echo "  + $dll_name"
    dll_count=$((dll_count + 1))
done < <(ldd "$STAGE_DIR/$EXE_NAME" 2>/dev/null \
    | grep -i '\.dll' \
    | grep -iv '/c/windows/' \
    | awk '{print $3}' \
    | sort -u)

for dll in "$BUILD_BIN"/*.dll; do
    [ -f "$dll" ] && cp "$dll" "$STAGE_DIR/"
done
echo "Bundled $dll_count DLLs."

# --- zip -----------------------------------------------------------------------
rm -f "$NAME.zip"

# Try each zipper and check it actually produced the file -- being installed and
# working are different things (this box has a 7z that resolves but can't run).
# PowerShell's Compress-Archive is last because it's the one always present on
# Windows, so it's the reliable floor rather than a first choice.
zip -rq "$NAME.zip" "$STAGE_DIR" 2>/dev/null || true
if [ ! -f "$NAME.zip" ]; then
    7z a -bso0 "$NAME.zip" "$STAGE_DIR" >/dev/null 2>&1 || true
fi
if [ ! -f "$NAME.zip" ]; then
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$STAGE_DIR' -DestinationPath '$NAME.zip' -Force" >/dev/null 2>&1 || true
fi
if [ ! -f "$NAME.zip" ]; then
    echo "ERROR: could not create $NAME.zip (no working zip / 7z / PowerShell)." >&2
    echo "The staged folder is left at ./$STAGE_DIR -- zip it by hand." >&2
    exit 1
fi

rm -rf "$STAGE_DIR"

echo
echo "Created $NAME.zip -- share this file."
echo "It carries its own assets + config; unzip anywhere and run $EXE_NAME."
