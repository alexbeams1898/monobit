#!/usr/bin/env bash
# Convenience wrapper for rebookend_clip.py. Picks up Blender from its
# default Windows install location and forwards all args through.
#
# Usage:
#   ./rebookend.sh \
#       --source "<source.fbx>" --source-time <seconds> \
#       --target "<target.fbx>" --target-blend-frames <N> \
#       --output "<output.fbx>"
#
# Run from the repo root so relative paths in args resolve sensibly.

set -euo pipefail

BLENDER="/c/Program Files/Blender Foundation/Blender 5.1/blender.exe"
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/rebookend_clip.py"

if [[ ! -x "$BLENDER" ]]; then
    echo "ERROR: Blender not found at: $BLENDER" >&2
    echo "  Install Blender 5.1 or update BLENDER path in this script." >&2
    exit 1
fi
if [[ ! -f "$SCRIPT" ]]; then
    echo "ERROR: rebookend_clip.py not found at: $SCRIPT" >&2
    exit 1
fi

# Run Blender headless. --factory-startup avoids loading the user's
# saved preferences (which can break unattended runs).
"$BLENDER" \
    --background \
    --factory-startup \
    --python "$SCRIPT" \
    -- "$@"
