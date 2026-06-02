"""Re-export Quaternius's Wolf.blend as a clean glTF.

The original glTF in the Ultimate Animated Animals pack has
quaternion tracks that aren't perfectly unit-length (close, but
fail ozz's IsNormalizedEst assertion at sample time). Source of
truth is the .blend; Blender's glTF exporter normalizes quaternions
on export. This script just opens the .blend and re-exports.

Run via Blender CLI:
    "/c/Program Files/Blender Foundation/Blender 5.1/blender.exe" \
        --background --python reexport_quaternius_wolf.py

Reads:
    games/selva-oscura/assets/characters/wolf/source/Wolf.blend
Writes:
    games/selva-oscura/assets/characters/wolf/source/wolf.gltf  (overwrites)
"""

import os
import sys

import bpy  # type: ignore

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
SRC = os.path.join(REPO_ROOT, "games", "selva-oscura", "assets", "characters", "wolf", "source",
                   "Wolf.blend")
# Output as .glb (single-file binary; Blender 5.1+ dropped
# GLTF_EMBEDDED format). cgltf reads either extension, and gltf2ozz
# accepts either input format too. Replaces the prior wolf.gltf with
# wolf.glb -- the engine's loadBundle("wolf", "wolf.gltf") setting in
# SkeletalAssets.cpp gets flipped to "wolf.glb" alongside.
DST = os.path.join(REPO_ROOT, "games", "selva-oscura", "assets", "characters", "wolf", "source",
                   "wolf.glb")


def main() -> None:
    if not os.path.isfile(SRC):
        raise SystemExit(f"source not found: {SRC}")

    bpy.ops.wm.open_mainfile(filepath=SRC)
    sys.stderr.write(f"[reexport-wolf] opened {SRC}\n")

    # Force pose_position = POSE so animations export properly (default
    # behavior, but explicit). Blender's glTF exporter normalizes
    # quaternion tracks during export -- that's the load-bearing fix.
    for arm in bpy.data.objects:
        if arm.type == "ARMATURE":
            arm.data.pose_position = "POSE"

    # Export as .glb (single-file binary). Blender 5.1+ removed
    # GLTF_EMBEDDED; GLB is the equivalent self-contained format.
    bpy.ops.export_scene.gltf(
        filepath=DST,
        export_format="GLB",
        export_apply=False,       # keep modifiers as-is; skinning relies on them
        export_animations=True,
        export_skins=True,
        export_yup=True,
    )
    sys.stderr.write(f"[reexport-wolf] wrote {DST}\n")


if __name__ == "__main__":
    main()
