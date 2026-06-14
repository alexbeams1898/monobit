#!/usr/bin/env bash
# Convenience wrapper for overlay_hand_pose.py. Picks up Blender from
# its default Windows install location and forwards all args through.
#
# Usage:
#   ./overlay_hand_pose.sh \
#       --source "<source.fbx>" --source-time <seconds> \
#       --target "<target.fbx>" \
#       --output "<output.fbx>" \
#       [--bones right_hand_grip]
#
# Run from the repo root so relative paths in args resolve sensibly.

set -euo pipefail

BLENDER="/c/Program Files/Blender Foundation/Blender 5.1/blender.exe"
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/overlay_hand_pose.py"

if [[ ! -x "$BLENDER" ]]; then
    echo "ERROR: Blender not found at: $BLENDER" >&2
    echo "  Install Blender 5.1 or update BLENDER path in this script." >&2
    exit 1
fi
if [[ ! -f "$SCRIPT" ]]; then
    echo "ERROR: overlay_hand_pose.py not found at: $SCRIPT" >&2
    exit 1
fi

"$BLENDER" \
    --background \
    --factory-startup \
    --python "$SCRIPT" \
    -- "$@"
