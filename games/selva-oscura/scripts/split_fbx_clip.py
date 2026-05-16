"""Split a Mixamo FBX into multiple sub-range FBX files.

Run via Blender CLI:
    blender --background --python split_fbx_clip.py -- \
        --src <source.fbx> \
        --out-dir <output_dir> \
        --range NAME=START..END [--range ...]

Example:
    blender --background --python split_fbx_clip.py -- \
        --src .../unarmed_block.fbx \
        --out-dir .../source/combat/unarmed/ \
        --range raise=0..18 \
        --range idle=18..22 \
        --range lower=18..48

Frame numbers are 1-indexed end-inclusive (matches Blender's
Frame Start/End conventions).

The script imports the source FBX, then for each range:
- Sets Scene Frame Start/End to that range
- Exports the rig + animation, FBX-only, with the trimmed range baked
- Writes <out_dir>/<src_stem>_<NAME>.fbx
"""

import argparse
import os
import sys

# Blender imports — only available inside Blender's Python env.
import bpy  # type: ignore


def parse_range_spec(spec: str) -> tuple[str, int, int, int | None]:
    """Parse NAME=START..END or NAME=FRAME (freeze form).

    Freeze form: NAME=FRAME exports a 2-keyframe animation where both
    keyframes hold the pose at FRAME — clip-time 0 and clip-time
    (clone_to / 30s). ozz needs 2 distinct keyframe times to validate,
    but the POSE can be identical on both, producing a genuine
    no-motion freeze without the 1/30s-twitch problem.
    """
    if "=" not in spec:
        raise SystemExit(f"Bad --range spec: {spec!r} (want NAME=START..END or NAME=FRAME)")
    name, rng = spec.split("=", 1)
    name = name.strip()
    if ".." in rng:
        start_s, end_s = rng.split("..", 1)
        return name, int(start_s), int(end_s), None
    # Freeze form: a single frame number. We clone that pose to a far-
    # away frame (default frame 30) so ozz sees two distinct keyframe
    # times but identical poses.
    f = int(rng)
    return name, f, f, 30  # clone-to default


def main() -> int:
    # Blender's argv has "--" before script args. Strip everything up to and including it.
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []

    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--range", dest="ranges", action="append", required=True)
    args = ap.parse_args(argv)

    src_path = os.path.abspath(args.src)
    out_dir = os.path.abspath(args.out_dir)
    if not os.path.isfile(src_path):
        raise SystemExit(f"Source FBX not found: {src_path}")
    os.makedirs(out_dir, exist_ok=True)

    src_stem = os.path.splitext(os.path.basename(src_path))[0]

    # Clean scene, import the source FBX.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src_path)
    bpy.context.scene.render.fps = 30

    # The bake pipeline (gltf2ozz) names its output by the animation's
    # internal name, then renames mixamo.com.ozz to <clip>.ozz. Mixamo
    # FBXs ship with action name "mixamo.com"; Blender re-exports
    # default to action name "Scene" which produces Scene.ozz and
    # breaks the rename step. Force the action name back to mixamo.com
    # so the bake works without modification.
    for action in bpy.data.actions:
        action.name = "mixamo.com"

    # NOTE: freeze-form (--range NAME=FRAME) inserts cloned keyframes
    # into the scene's actions. Run freeze-form ranges LAST so the
    # cloned keys don't pollute earlier range exports.
    for spec in args.ranges:
        name, start_f, end_f, clone_to = parse_range_spec(spec)
        out_path = os.path.join(out_dir, f"{src_stem}_{name}.fbx")

        if clone_to is not None:
            # Freeze form: clone every f-curve's pose at start_f to
            # start_f+clone_to so ozz sees two distinct keyframe times
            # with identical poses. Blender 5.x layered-action API:
            # action.layers[].strips[].channelbag(slot).fcurves.
            target_f = start_f + clone_to
            print(f"[split] {name}: freeze pose@{start_f} cloned to {target_f} -> {out_path}")
            total_curves = 0
            for action in bpy.data.actions:
                for layer in action.layers:
                    for strip in layer.strips:
                        for slot in action.slots:
                            cbag = strip.channelbag(slot)
                            if cbag is None:
                                continue
                            for fcurve in cbag.fcurves:
                                val_at_start = fcurve.evaluate(start_f)
                                # Delete all existing keyframes in
                                # [start_f, target_f] so the freeze's
                                # anchor keyframes are the ONLY ones
                                # in the export range. Iterate
                                # backwards because removal shifts
                                # indices.
                                to_remove = [
                                    i
                                    for i, kp in enumerate(fcurve.keyframe_points)
                                    if start_f <= kp.co.x <= target_f
                                ]
                                for i in reversed(to_remove):
                                    fcurve.keyframe_points.remove(
                                        fcurve.keyframe_points[i], fast=True
                                    )
                                # Insert anchor keyframes at both ends
                                # with CONSTANT interp so the value
                                # stays flat between them.
                                k_start = fcurve.keyframe_points.insert(
                                    start_f, val_at_start, options={"FAST"}
                                )
                                k_end = fcurve.keyframe_points.insert(
                                    target_f, val_at_start, options={"FAST"}
                                )
                                k_start.interpolation = "CONSTANT"
                                k_end.interpolation = "CONSTANT"
                                total_curves += 1
                            for fcurve in cbag.fcurves:
                                fcurve.update()
            print(f"[split]   cloned {total_curves} fcurves with CONSTANT interp")
            bpy.context.scene.frame_start = start_f
            bpy.context.scene.frame_end = target_f
        else:
            print(f"[split] {name}: frames {start_f}..{end_f} -> {out_path}")
            bpy.context.scene.frame_start = start_f
            bpy.context.scene.frame_end = end_f

        bpy.ops.export_scene.fbx(
            filepath=out_path,
            use_selection=False,
            use_visible=True,
            object_types={"ARMATURE", "MESH", "EMPTY"},
            bake_anim=True,
            bake_anim_use_all_bones=True,
            bake_anim_use_nla_strips=False,
            bake_anim_use_all_actions=False,
            bake_anim_force_startend_keying=True,
            bake_anim_step=1.0,
            bake_anim_simplify_factor=0.0,
            add_leaf_bones=False,
            armature_nodetype="NULL",
            path_mode="AUTO",
        )

    print("[split] done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
