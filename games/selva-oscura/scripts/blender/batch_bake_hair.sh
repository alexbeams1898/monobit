#!/bin/bash
# Batch-bake every hair style under assets/characters/hair/{cc0,ccby}/
# into a standalone rigged .glb via gen_hair.py.
#
# Per-style discovery:
#   - Style folder = one per subdir under cc0/ or ccby/
#   - .mhclo file  = any *.mhclo in that folder (first match wins)
#   - Diffuse PNG  = read the mhclo's sibling .mhmat and pick out the
#                    diffuseTexture line; that filename is the actual
#                    diffuse (some styles ship `<style>_diffuse.png`,
#                    others ship a shorter name like `afro_diffuse.png`).
#
# Output lands next to the source .mhclo as `humanoid_male_hair.glb`.
# Later, once the female clip pipeline stabilizes, re-run this with
# --gender female and it'll drop `humanoid_female_hair.glb` beside.
#
# Skips styles that already have humanoid_<gender>_hair.glb newer than
# their .mhclo (idempotent -- re-runs only touch what changed).
#
# Usage:
#   bash games/selva-oscura/scripts/blender/batch_bake_hair.sh [male|female]
#
# Defaults to male.

set -e

GENDER="${1:-male}"
REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
HAIR_ROOT="$REPO_ROOT/games/selva-oscura/assets/characters/hair"
GEN_HAIR="$REPO_ROOT/games/selva-oscura/scripts/blender/gen_hair.py"
BLENDER="/c/Program Files/Blender Foundation/Blender 4.2/blender.exe"

if [ ! -x "$BLENDER" ]; then
    echo "[batch] Blender 4.2 not at expected path: $BLENDER" >&2
    exit 1
fi

if [ ! -d "$HAIR_ROOT" ]; then
    echo "[batch] hair root not found: $HAIR_ROOT" >&2
    exit 1
fi

TOTAL=0
BAKED=0
SKIPPED=0
FAILED=0

# Iterate license buckets (cc0, ccby) then per-style subdirs.
for license in cc0 ccby; do
    lic_dir="$HAIR_ROOT/$license"
    [ -d "$lic_dir" ] || continue
    for style_dir in "$lic_dir"/*/; do
        [ -d "$style_dir" ] || continue
        style_id="$(basename "$style_dir")"
        TOTAL=$((TOTAL+1))

        # Find the mhclo (there's exactly one per style folder in
        # every pack we downloaded; guard against zero for safety).
        mhclo="$(find "$style_dir" -maxdepth 1 -name '*.mhclo' | head -n1)"
        if [ -z "$mhclo" ]; then
            echo "[batch] $style_id: no .mhclo found; skipping" >&2
            FAILED=$((FAILED+1))
            continue
        fi

        # Find the mhmat (material file) and extract the diffuseTexture
        # line's filename. Some styles ship the diffuse next to the
        # mhclo with a name that differs from the style_id (e.g.
        # afro01/ ships afro_diffuse.png, not afro01_diffuse.png).
        mhmat="$(find "$style_dir" -maxdepth 1 -name '*.mhmat' | head -n1)"
        diffuse_name=""
        if [ -n "$mhmat" ]; then
            diffuse_name="$(grep -E '^diffuseTexture ' "$mhmat" | awk '{print $2}' | tr -d '\r')"
        fi
        if [ -z "$diffuse_name" ]; then
            # Fallback: any *_diffuse.png in the folder.
            diffuse_name="$(find "$style_dir" -maxdepth 1 -name '*_diffuse.png' -o -name '*Diffuse*.png' | head -n1 | xargs -I{} basename {})"
        fi
        if [ -z "$diffuse_name" ]; then
            # Last-ditch: any PNG.
            diffuse_name="$(find "$style_dir" -maxdepth 1 -name '*.png' | head -n1 | xargs -I{} basename {})"
        fi
        diffuse_path=""
        if [ -n "$diffuse_name" ]; then
            diffuse_path="$style_dir/$diffuse_name"
            if [ ! -f "$diffuse_path" ]; then
                # Some mhmat files reference the PNG by relative path;
                # try resolving against the mhmat's directory.
                diffuse_path="$(dirname "$mhmat")/$diffuse_name"
            fi
        fi

        out_glb="$style_dir/humanoid_${GENDER}_hair.glb"

        # Skip if output is newer than the mhclo AND newer than the
        # diffuse (idempotent re-runs).
        if [ -f "$out_glb" ]; then
            skip=1
            [ "$mhclo" -nt "$out_glb" ] && skip=0
            [ -n "$diffuse_path" ] && [ -f "$diffuse_path" ] && [ "$diffuse_path" -nt "$out_glb" ] && skip=0
            if [ "$skip" -eq 1 ]; then
                SKIPPED=$((SKIPPED+1))
                echo "[batch] $style_id: up-to-date; skipping"
                continue
            fi
        fi

        # Build the invocation. Diffuse arg only if we found one.
        args=(--gender "$GENDER" --hair-mhclo "$mhclo" --out-glb "$out_glb")
        if [ -n "$diffuse_path" ] && [ -f "$diffuse_path" ]; then
            args+=(--hair-diffuse "$diffuse_path")
        fi

        echo "[batch] baking $license/$style_id ..."
        if "$BLENDER" --background --python "$GEN_HAIR" -- "${args[@]}" > /dev/null 2>&1; then
            BAKED=$((BAKED+1))
            size=$(stat -c %s "$out_glb" 2>/dev/null || stat -f %z "$out_glb")
            echo "[batch]   ok ($size bytes)"
        else
            FAILED=$((FAILED+1))
            echo "[batch]   FAILED" >&2
        fi
    done
done

echo ""
echo "[batch] total=$TOTAL baked=$BAKED skipped=$SKIPPED failed=$FAILED"
if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
