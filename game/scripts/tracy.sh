#!/usr/bin/env bash
# Launch the Tracy profiler GUI.
# Run this first, then launch the game — Tracy connects automatically.
#
# Requires the game to be built with -DTRACY_ENABLE=ON (set in .vscode/settings.json).
# Tracy binary lives in tools/tracy/ (gitignored — re-download if missing).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACY="$SCRIPT_DIR/../tools/tracy/tracy-profiler.exe"

if [[ ! -f "$TRACY" ]]; then
    echo "Tracy not found at tools/tracy/tracy-profiler.exe"
    echo "Download windows-0.11.1.zip from https://github.com/wolfpld/tracy/releases/tag/v0.11.1"
    echo "and extract it to tools/tracy/"
    exit 1
fi

echo "Launching Tracy profiler — connect your game build to start recording."
"$TRACY" &
