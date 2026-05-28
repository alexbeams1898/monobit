#!/usr/bin/env bash
# package.sh -- create a shareable zip of selva-oscura.
#
# Usage:
#   ./games/selva-oscura/scripts/package.sh [output-name]
#
# Bundles the game exe + assets + config into a zip ready to share.
# Output name defaults to "selva-oscura-v<VERSION>" where VERSION is read
# from this game's CMakeLists.txt project() declaration. Creates <name>.zip in
# repo root.

set -e

# grep -P needs a UTF-8 locale on MSYS2 / MinGW. The default user
# locale may be empty or non-UTF-8, which breaks version extraction.
export LC_ALL=C.UTF-8

# Resolve script dir / game dir / repo root from this script's location, so
# package.sh can be invoked from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(cd "$GAME_DIR/../.." && pwd)"

cd "$REPO_ROOT"

BUILD_BIN="build/bin"
EXE_NAME="selva-oscura.exe"  # CMake target name

# Extract version from this game's CMakeLists.txt: project(... VERSION X.Y.Z ...)
VERSION=$(grep -oP 'project\([^)]*VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "$GAME_DIR/CMakeLists.txt" || echo "unknown")
NAME="${1:-selva-oscura-v$VERSION}"
STAGE_DIR="$NAME"

if [ ! -f "$BUILD_BIN/$EXE_NAME" ]; then
    echo "ERROR: $BUILD_BIN/$EXE_NAME not found. Build the game first (F7)." >&2
    exit 1
fi

# Clean previous staging.
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

# Copy game exe.
cp "$BUILD_BIN/$EXE_NAME" "$STAGE_DIR/$NAME.exe"

# Copy runtime data. Exclude `source/` subdirs (raw FBX/zip authoring
# sources that bake into runtime .ozz/.glb — shipped artifacts are
# already in place). Also exclude any stray .blend / .fbx / .zip the
# bake might have left behind under non-source paths.
mkdir -p "$STAGE_DIR/assets"
if command -v rsync >/dev/null 2>&1; then
    rsync -a --exclude='source/' --exclude='*.blend' --exclude='*.blend1' \
        --exclude='*.fbx' --exclude='*.zip' \
        "$BUILD_BIN/assets/" "$STAGE_DIR/assets/"
else
    cp -r "$BUILD_BIN/assets/." "$STAGE_DIR/assets/"
    find "$STAGE_DIR/assets" -type d -name source -prune -exec rm -rf {} +
    find "$STAGE_DIR/assets" -type f \( -name '*.blend' -o -name '*.blend1' \
        -o -name '*.fbx' -o -name '*.zip' \) -delete
fi
cp -r "$BUILD_BIN/config" "$STAGE_DIR/config"

# Auto-detect and bundle all non-system DLLs the exe needs.
# Uses ldd to find every dependency, then filters out Windows system DLLs
# (anything under /c/windows). This catches MinGW runtime libs, SDL2, etc.
# automatically -- no manual DLL list to maintain.
echo "Scanning DLL dependencies..."
dll_count=0
while IFS= read -r dll_path; do
    dll_name=$(basename "$dll_path")
    cp "$dll_path" "$STAGE_DIR/$dll_name"
    echo "  + $dll_name"
    dll_count=$((dll_count + 1))
done < <(ldd "$STAGE_DIR/$NAME.exe" 2>/dev/null \
    | grep -i '\.dll' \
    | grep -iv '/c/windows/' \
    | awk '{print $3}' \
    | sort -u)

# Also grab any DLLs that ended up in the build dir (e.g. from FetchContent).
for dll in "$BUILD_BIN"/*.dll; do
    [ -f "$dll" ] && cp "$dll" "$STAGE_DIR/"
done
echo "Bundled $dll_count DLLs."

# Zip it up.
rm -f "$NAME.zip"
# Prefer `zip` (CI installs it via msys2 setup). Fall back to PowerShell
# Compress-Archive on local Windows where `zip` isn't installed and the
# MSYS2 `7z` shim is broken.
if command -v zip >/dev/null 2>&1; then
    zip -r "$NAME.zip" "$STAGE_DIR"
elif command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$STAGE_DIR' -DestinationPath '$NAME.zip' -Force"
elif command -v 7z >/dev/null 2>&1; then
    7z a "$NAME.zip" "$STAGE_DIR"
else
    echo "ERROR: no zip tool found (zip / powershell.exe / 7z)." >&2
    exit 1
fi

# Clean up staging dir.
rm -rf "$STAGE_DIR"

echo "Created $NAME.zip -- share this file."
