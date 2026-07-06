#!/usr/bin/env bash
# Bake every Mixamo .fbx under <clips_in_root> to .glb under <clips_out>.
# Recurses into pack subdirectories. Skips:
#   - humanoid_male.fbx (Mixamo bundles the character with pack downloads)
#   - numbered duplicates like "name (2).fbx" (prefer the primary variant)
# Output filename = input stem lowercased, spaces -> underscores.
#
# Usage:
#   bash games/selva-oscura/scripts/blender/batch_bake.sh \
#       <clips_in_root> <clips_out> <humanoid_male.blend>
set -euo pipefail

CLIPS_IN_ROOT="${1:?clips_in root dir}"
CLIPS_OUT="${2:?clips out dir}"
CHAR_BLEND="${3:?humanoid_male.blend}"
BLENDER="/c/Program Files/Blender Foundation/Blender 4.2/blender.exe"
BAKE_SCRIPT="$(dirname "$0")/bake_clip.py"

mkdir -p "$CLIPS_OUT"

ok=0
skip=0
fail=0

# Specific numbered Mixamo variants we want to KEEP (rescued from the
# dup-skip rule below) and bake under a renamed output stem. Keyed by
# the raw lowercased+underscored basename (with the "_N" suffix from
# the parens variant); value is the output stem we ship as. Listed
# here so each rescue is intentional + grep-able.
declare -A VARIANT_KEEPS=(
    [zombie_biting_2]=zombie_biting_ground
)

# Pass 1: index every .fbx by its CANONICAL stem (strip trailing
# "(N)" download/variant suffix). Mixamo names clip variants with
# (2)/(3)/etc., and the browser adds "(1)" when a file already exists
# in the download folder. We keep one .fbx per canonical stem,
# preferring the lowest N (primary > (1) > (2) > ...). Skip the
# bundled character mesh. VARIANT_KEEPS entries bypass dedup + bake
# under their renamed stem.
declare -A picked   # canonical_stem -> chosen fbx path
declare -A picked_n # canonical_stem -> chosen N (0 = primary)
declare -A picked_out_override # canonical_stem -> renamed output stem (from VARIANT_KEEPS)
while IFS= read -r -d '' fbx; do
    base="$(basename "$fbx" .fbx)"
    [[ "$base" == "humanoid_male" || "$base" == "humanoid_female" ]] && { skip=$((skip+1)); continue; }
    # VARIANT_KEEPS check: lowercased+underscored basename including
    # the "_N" suffix. Treats this file as its own canonical stem so
    # dedup doesn't collapse it with the primary.
    base_key="$(echo "$base" | tr '[:upper:] ' '[:lower:]_' | sed -E 's/_\(([0-9]+)\)$/_\1/')"
    if [[ -n "${VARIANT_KEEPS[$base_key]+x}" ]]; then
        canonical="$base_key"  # unique per variant
        picked[$canonical]="$fbx"
        picked_n[$canonical]=0
        picked_out_override[$canonical]="${VARIANT_KEEPS[$base_key]}"
        continue
    fi
    n=0
    canonical="$base"
    if [[ "$base" =~ ^(.*)\ \(([0-9]+)\)$ ]]; then
        canonical="${BASH_REMATCH[1]}"
        n="${BASH_REMATCH[2]}"
    fi
    if [[ -z "${picked[$canonical]+x}" ]] || (( n < ${picked_n[$canonical]} )); then
        # Demoted previous pick (if any) counts as skipped.
        [[ -n "${picked[$canonical]+x}" ]] && skip=$((skip+1))
        picked[$canonical]="$fbx"
        picked_n[$canonical]="$n"
    else
        skip=$((skip+1))
    fi
done < <(find "$CLIPS_IN_ROOT" -type f -iname '*.fbx' -print0)

# Project-local output-name overrides. Mixamo's canonical names don't
# always match the names the engine + design docs reference; map them
# here so re-bakes stay stable. Key = lowercased+underscored Mixamo
# stem; value = the stem we ship as.
declare -A NAME_MAP=(
    [jog_forward]=jogging
    [jog_backward]=jogging_backward
    [jog_strafe_left]=strafe_jogging_left
    [jog_strafe_right]=strafe_jogging_right
    [left_strafe_run]=strafe_running_left
    [right_strafe_run]=strafe_running_right
    [left_strafe_walking]=strafe_walking_left
    [right_strafe_walking]=strafe_walking_right
    [walking_backwards]=walking_backward
    [combat_idle]=unarmed_combat_idle
)

# Pass 2: bake each canonical clip.
for canonical in "${!picked[@]}"; do
    fbx="${picked[$canonical]}"
    # VARIANT_KEEPS override wins; else NAME_MAP rename; else raw stem.
    if [[ -n "${picked_out_override[$canonical]+x}" ]]; then
        out_stem="${picked_out_override[$canonical]}"
    else
        raw_stem="$(echo "$canonical" | tr '[:upper:] ' '[:lower:]_')"
        out_stem="${NAME_MAP[$raw_stem]:-$raw_stem}"
    fi
    out="$CLIPS_OUT/${out_stem}.glb"
    printf '[batch_bake] %-44s -> %s\n' "$(basename "$fbx" .fbx)" "${out_stem}.glb"
    if "$BLENDER" --background --python "$BAKE_SCRIPT" -- \
            --char "$CHAR_BLEND" --src "$fbx" --out "$out" \
            >/dev/null 2>&1; then
        ok=$((ok+1))
    else
        printf '  FAIL: %s\n' "$fbx" >&2
        fail=$((fail+1))
    fi
done

echo "[batch_bake] ok=$ok skip=$skip fail=$fail"
