#!/usr/bin/env bash
# package.sh -- create a shareable zip of the game.
#
# Usage:
#   ./game/scripts/package.sh [output-name]
#
# Bundles the game exe + assets + config into a zip ready to share.
# Output name defaults to "hell-escape". Creates hell-escape.zip in repo root.

set -e

BUILD_BIN="build/bin"
NAME="${1:-hell-escape}"
STAGE_DIR="$NAME"

if [ ! -f "$BUILD_BIN/prison-break-game.exe" ]; then
    echo "ERROR: $BUILD_BIN/prison-break-game.exe not found. Build the game first (F7)." >&2
    exit 1
fi

# Clean previous staging.
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

# Copy game exe.
cp "$BUILD_BIN/prison-break-game.exe" "$STAGE_DIR/$NAME.exe"

# Copy runtime data.
cp -r "$BUILD_BIN/assets" "$STAGE_DIR/assets"
cp -r "$BUILD_BIN/config" "$STAGE_DIR/config"

# Copy any DLLs the exe needs (SDL2, etc.).
for dll in "$BUILD_BIN"/*.dll; do
    [ -f "$dll" ] && cp "$dll" "$STAGE_DIR/"
done

# Zip it up.
rm -f "$NAME.zip"
if command -v zip >/dev/null 2>&1; then
    zip -r "$NAME.zip" "$STAGE_DIR"
elif command -v 7z >/dev/null 2>&1; then
    7z a "$NAME.zip" "$STAGE_DIR"
else
    # Fallback: PowerShell's Compress-Archive (available on all Windows).
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$STAGE_DIR' -DestinationPath '$NAME.zip' -Force"
fi

# Clean up staging dir.
rm -rf "$STAGE_DIR"

echo "Created $NAME.zip -- share this file."
