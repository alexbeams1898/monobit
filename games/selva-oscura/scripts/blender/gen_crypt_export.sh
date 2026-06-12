#!/usr/bin/env bash
# Wrapper for gen_crypt_export.py — opens crypt_foundation.blend in
# headless Blender and exports per-region .glb files for engine
# loading (one .glb per collection in the .blend).
#
# Usage:
#   bash games/selva-oscura/scripts/blender/gen_crypt_export.sh
#
# Input:  games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend
# Outputs:
#   games/selva-oscura/assets/world/static_meshes/chapel_exterior.glb
#   games/selva-oscura/assets/world/static_meshes/chapel_interior.glb

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SCRIPT="${SCRIPT_DIR}/gen_crypt_export.py"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
BLEND_INPUT="${REPO_ROOT}/games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend"

BLENDER_EXE="/c/Program Files/Blender Foundation/Blender 5.1/blender.exe"
if [ ! -f "${BLENDER_EXE}" ]; then
    echo "[gen_crypt_export] Blender 5.1 not found at: ${BLENDER_EXE}" >&2
    exit 1
fi
if [ ! -f "${BLEND_INPUT}" ]; then
    echo "[gen_crypt_export] Source .blend not found: ${BLEND_INPUT}" >&2
    echo "[gen_crypt_export] Run gen_crypt_foundation.sh first." >&2
    exit 1
fi

"${BLENDER_EXE}" --background "${BLEND_INPUT}" --python "${PY_SCRIPT}"
