"""
Re-book-end a Mixamo FBX clip's first N frames so they smoothly continue
from a *source* clip's pose at a specified frame.

Why this exists
---------------
AAA action games author each chain swing's END pose to land on the NEXT
swing's anticipation pose, so blends at the splice are nearly invisible.
Mixamo packs ship clips that each return to a generic combat-stance
neutral instead — so consecutive Mixamo swings have mismatched bookend
poses, producing a visible "jerk" at the chain splice.

This script fixes the mismatch at the *asset* level: it imports a SOURCE
clip and a TARGET clip, samples the source's pose at a chosen
recovery-time, and overwrites (with a smooth blend) the target's first N
frames so the target now begins where the source ended. The blend is a
per-bone quaternion slerp + position lerp from (source pose) to (target's
authored pose at frame N), so by frame N the target is back to its
original animation. Frames > N are untouched.

Usage (run from a terminal — Blender CLI, no GUI):
    blender --background --python rebookend_clip.py -- \\
        --source "<source.fbx>" --source-time <seconds> \\
        --target "<target.fbx>" --target-blend-frames <N> \\
        --output "<output.fbx>"

Arguments after the `--` are passed to the script (Blender convention).

Example
-------
slash_3 (the "chain link 1" attack) is jerky after slash. We want to
rebook-end slash_3's first 8 frames so they continue from slash's pose at
slash's cancel-open instant (~0.683s into slash):

    blender --background --python rebookend_clip.py -- \\
        --source ".../sword and shield slash.fbx" \\
        --source-time 0.683 \\
        --target ".../sword and shield slash (3).fbx" \\
        --target-blend-frames 8 \\
        --output ".../sword and shield slash (3) rebookended.fbx"

Drop the output FBX into a `Reposed/` source dir, add that dir to
CMakeLists.txt's mixamo_pack_dirs, rebuild — the existing FBX2glTF +
gltf2ozz pipeline will pick it up and produce a new .ozz next to the
others.

Invariants
----------
- Source and target must share an identical armature/skeleton (Mixamo
  rigs all use mixamorig:* bone names; this is satisfied by every clip
  in the same pack).
- The output FBX inherits the target's animation track length and frame
  rate; only the first N frames are altered.
- Bones not present in the source are left at the target's authored pose
  for the entire blend (no fabricated motion).
"""

from __future__ import annotations
import argparse
import os
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector


def parse_args() -> argparse.Namespace:
    """Pull script args from after '--' on Blender's CLI."""
    if "--" in sys.argv:
        argv = sys.argv[sys.argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser(description="Rebookend a Mixamo FBX clip.")
    parser.add_argument(
        "--source", required=True, help="Source FBX whose pose at --source-time we copy from."
    )
    parser.add_argument(
        "--source-time",
        type=float,
        required=True,
        help="Time in source clip (seconds) whose pose is the new bookend start.",
    )
    parser.add_argument(
        "--target", required=True, help="Target FBX whose first --target-blend-frames are replaced."
    )
    parser.add_argument(
        "--target-blend-frames",
        type=int,
        default=8,
        help="How many frames at the start of target to blend (default 8 = ~0.27s at 30fps).",
    )
    parser.add_argument(
        "--output", required=True, help="Output FBX path. Parent directory must exist."
    )
    parser.add_argument(
        "--bones",
        default="upper_body",
        help=(
            "Which bones to re-pose. 'upper_body' (default) = spine + both arms + neck/head; "
            "'right_arm' = just the right arm chain (use when ONLY the weapon hand is "
            "mismatched and legs/spine should stay authored); 'all' = every bone (legacy, "
            "tends to corrupt leg motion). Comma-separated bone-name prefixes also accepted, "
            "e.g. 'mixamorig:RightShoulder,mixamorig:RightArm'."
        ),
    )
    return parser.parse_args(argv)


# Mixamo bone groups for selective pose-copy. Each group is a list of
# bone-name prefixes; matching is done with str.startswith(). The
# 'upper_body' set covers spine + both arms + head/neck — anything that
# tends to be authored differently between two combat clips. 'right_arm'
# is the minimum needed to fix a sword-side bookend mismatch without
# touching anything else (legs stay authored, off-hand stays authored).
BONE_GROUPS: dict[str, list[str]] = {
    "all": [],  # empty = wildcard (matches everything)
    "upper_body": [
        "mixamorig:Spine",
        "mixamorig:Spine1",
        "mixamorig:Spine2",
        "mixamorig:Neck",
        "mixamorig:Head",
        "mixamorig:LeftShoulder",
        "mixamorig:LeftArm",
        "mixamorig:LeftForeArm",
        "mixamorig:LeftHand",
        "mixamorig:RightShoulder",
        "mixamorig:RightArm",
        "mixamorig:RightForeArm",
        "mixamorig:RightHand",
    ],
    "right_arm": [
        "mixamorig:RightShoulder",
        "mixamorig:RightArm",
        "mixamorig:RightForeArm",
        "mixamorig:RightHand",
    ],
}


def resolve_bone_filter(spec: str) -> list[str]:
    """Return the prefix list named by spec, or the literal csv if it's
    a comma-separated list of bone names. Empty list means 'all bones'.
    """
    if spec in BONE_GROUPS:
        return BONE_GROUPS[spec]
    # Treat as csv of bone-name prefixes.
    return [s.strip() for s in spec.split(",") if s.strip()]


def bone_matches(bone_name: str, prefixes: list[str]) -> bool:
    """True if `bone_name` starts with any prefix in `prefixes`. Empty
    `prefixes` means match-all (legacy 'all' mode).
    """
    if not prefixes:
        return True
    return any(bone_name.startswith(p) for p in prefixes)


def reset_blender() -> None:
    """Wipe the default scene so each invocation starts clean."""
    bpy.ops.wm.read_factory_settings(use_empty=True)


def iter_action_fcurves(action: bpy.types.Action):
    """Yield every f-curve in a Blender 5.x layered Action.

    The new (5.x+) model: Action -> layers[] -> strips[] (KEYFRAME) ->
    channelbags[] -> fcurves[]. We yield from every channelbag in every
    strip in every layer. The legacy `action.fcurves` flat attribute is
    gone in Blender 5.1.
    """
    for layer in action.layers:
        for strip in layer.strips:
            # Only KEYFRAME strips have channelbags; skip other strip types.
            if strip.type != "KEYFRAME":
                continue
            for cb in strip.channelbags:
                for fcurve in cb.fcurves:
                    yield fcurve


def import_fbx(path: Path) -> bpy.types.Object:
    """Import an FBX, set scene frame range to match the imported clip.

    Why the explicit frame-range fixup: Blender's scene defaults to
    frame_end=250 regardless of what the FBX contains. After import,
    the imported action can be only ~50 frames long but the scene
    timeline still says 1..250. When we later export with bake_anim=
    True, Blender bakes one keyframe per scene-frame across the whole
    250-frame range — most of which is the rest pose / hold of the
    last authored frame. Result: an 8.3-second .ozz clip from a
    1.57-second FBX, full of empty space and static frames that change
    motion-end / peak detection at the resolver. We sync the scene to
    the action's actual range so the export preserves the original
    timeline.
    """
    bpy.ops.import_scene.fbx(filepath=str(path))
    armature = next(
        (obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None
    )
    if armature is None:
        raise RuntimeError(f"No armature in {path}")
    # Sync scene frame range to imported action's range. Use the
    # tightest bound across all f-curves so we capture the full
    # authored timeline and nothing more.
    if armature.animation_data is not None and armature.animation_data.action is not None:
        action = armature.animation_data.action
        f_start = float("inf")
        f_end = float("-inf")
        for fcurve in iter_action_fcurves(action):
            for kp in fcurve.keyframe_points:
                if kp.co.x < f_start:
                    f_start = kp.co.x
                if kp.co.x > f_end:
                    f_end = kp.co.x
        if f_start <= f_end:
            scene = bpy.context.scene
            scene.frame_start = int(f_start)
            scene.frame_end = int(f_end)
    return armature


def sample_pose_at_time(armature: bpy.types.Object, t_seconds: float) -> dict[str, dict]:
    """Sample every bone's local rotation/location at clip-time t_seconds.

    Returns dict keyed by bone name: {bone: {"rot": Quaternion, "loc": Vector}}.
    """
    scene = bpy.context.scene
    fps = scene.render.fps / scene.render.fps_base
    target_frame = scene.frame_start + int(round(t_seconds * fps))
    target_frame = max(scene.frame_start, min(scene.frame_end, target_frame))
    scene.frame_set(target_frame)
    pose_data: dict[str, dict] = {}
    for pbone in armature.pose.bones:
        # Mixamo clips author rotation as quaternions — capture the pose-
        # bone's local rotation_quaternion (relative to rest). location
        # is local translation (relative to rest). Both are exactly what
        # FBX/glTF will round-trip on export.
        pose_data[pbone.name] = {
            "rot": pbone.rotation_quaternion.copy(),
            "loc": pbone.location.copy(),
        }
    return pose_data


def write_pose_at_frame(
    armature: bpy.types.Object,
    frame: int,
    pose_data: dict[str, dict],
    bone_filter: list[str],
) -> None:
    """Write rotation + location keyframes at scene frame `frame` for
    bones in pose_data WHOSE NAMES MATCH bone_filter. Bones outside the
    filter are skipped (their authored keyframes remain). Empty filter
    means every bone in pose_data is written.
    """
    scene = bpy.context.scene
    scene.frame_set(frame)
    for bone_name, data in pose_data.items():
        if not bone_matches(bone_name, bone_filter):
            continue
        pbone = armature.pose.bones.get(bone_name)
        if pbone is None:
            continue
        pbone.rotation_quaternion = data["rot"]
        pbone.location = data["loc"]
        pbone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
        pbone.keyframe_insert(data_path="location", frame=frame)


def get_authored_pose_at_frame(armature: bpy.types.Object, frame: int) -> dict[str, dict]:
    """Capture the target's currently-authored pose at scene frame `frame`
    BEFORE we overwrite anything. Used as the blend's destination so the
    target's frame-N pose is preserved and frames > N continue cleanly.
    """
    scene = bpy.context.scene
    scene.frame_set(frame)
    return {
        pbone.name: {"rot": pbone.rotation_quaternion.copy(), "loc": pbone.location.copy()}
        for pbone in armature.pose.bones
    }


def smoothstep(t: float) -> float:
    """Standard cubic ease-in-ease-out. t in [0,1]."""
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def blend_pose(
    src: dict[str, dict], dst: dict[str, dict], alpha: float
) -> dict[str, dict]:
    """Per-bone quaternion slerp + Vector lerp. alpha=0 = src, alpha=1 = dst."""
    out: dict[str, dict] = {}
    for bone_name, src_data in src.items():
        dst_data = dst.get(bone_name)
        if dst_data is None:
            out[bone_name] = {"rot": src_data["rot"].copy(), "loc": src_data["loc"].copy()}
            continue
        rot = src_data["rot"].slerp(dst_data["rot"], alpha)
        loc = src_data["loc"].lerp(dst_data["loc"], alpha)
        out[bone_name] = {"rot": rot, "loc": loc}
    return out


def clear_keyframes_in_range(
    armature: bpy.types.Object,
    frame_start: int,
    frame_end: int,
    bone_filter: list[str],
) -> None:
    """Remove bone-pose keyframes in [frame_start, frame_end] inclusive,
    but ONLY for f-curves whose data_path references a bone matching
    bone_filter. F-curves on bones outside the filter keep their
    authored keys — we want only the targeted bones to change, every
    other bone's authored motion must survive untouched.
    """
    if armature.animation_data is None or armature.animation_data.action is None:
        return
    action = armature.animation_data.action
    for fcurve in iter_action_fcurves(action):
        # Extract bone name from f-curve data_path. Mixamo paths look like
        # 'pose.bones["mixamorig:RightHand"].rotation_quaternion'.
        path = fcurve.data_path
        bone_name = ""
        if 'pose.bones["' in path:
            start = path.index('pose.bones["') + len('pose.bones["')
            end = path.index('"]', start)
            bone_name = path[start:end]
        if not bone_matches(bone_name, bone_filter):
            continue
        keys_to_remove = [
            kp for kp in fcurve.keyframe_points if frame_start <= kp.co.x <= frame_end
        ]
        for kp in keys_to_remove:
            fcurve.keyframe_points.remove(kp, fast=True)
        fcurve.update()


def rename_action_to_mixamo(armature: bpy.types.Object) -> None:
    """Force the armature's animation action AND the scene name to
    'mixamo.com'.

    Why both: the downstream pipeline reads the FBX *take* name (scene
    name when Blender exports), not the action name. Mixamo FBXs all
    have a take called "mixamo.com" — gltf2ozz writes the .ozz file
    using that name, and CMake's copy step expects exactly that
    filename. We set the scene name AND the action name so the export
    is consistent in both Blender's metadata and the FBX take metadata.
    """
    if armature.animation_data is not None and armature.animation_data.action is not None:
        armature.animation_data.action.name = "mixamo.com"
    bpy.context.scene.name = "mixamo.com"


def export_fbx(path: Path) -> None:
    """Export the current scene to FBX with Mixamo-compatible settings."""
    bpy.ops.export_scene.fbx(
        filepath=str(path),
        use_selection=False,
        bake_anim=True,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        add_leaf_bones=False,
        # Mixamo-like axis convention. FBX2glTF in our pipeline expects
        # what Mixamo ships, so we mirror the import defaults.
        axis_forward="-Z",
        axis_up="Y",
    )


def main() -> int:
    args = parse_args()
    src_path = Path(args.source).resolve()
    tgt_path = Path(args.target).resolve()
    out_path = Path(args.output).resolve()
    if not src_path.exists():
        print(f"ERROR: source not found: {src_path}", file=sys.stderr)
        return 1
    if not tgt_path.exists():
        print(f"ERROR: target not found: {tgt_path}", file=sys.stderr)
        return 1
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # Step 1: sample source at a single time. Earlier I tried a
    # trajectory-of-poses approach (sample source over N+1 successive
    # times so the bookend follows source's motion arc) — but that
    # bakes source's authored *motion* into the bookend region, which
    # produces "source clip continues playing during target's intended
    # windup" — visibly conflicting with target. Single-frozen-pose
    # bookend is the lesser-bad option; it produces continuous *pose*
    # without continuous motion. The unnatural intermediates the
    # slerp passes through are still a problem when target's authored
    # pose at frame N differs significantly in joint orientation, but
    # we accept that as the limit of automated bookending. Hand-
    # authored transition frames in Blender are the only further
    # improvement available with this asset set.
    print(f"[rebookend] sampling source: {src_path.name} @ t={args.source_time:.3f}s")
    reset_blender()
    src_armature = import_fbx(src_path)
    src_pose = sample_pose_at_time(src_armature, args.source_time)
    blend_n = max(1, args.target_blend_frames)

    # Step 2: load target fresh, capture its authored pose at frame N
    # (the blend destination — what the body should look like once the
    # blend completes), then clear keyframes in [0, N] and write the
    # trajectory-blended sequence.
    print(f"[rebookend] loading target: {tgt_path.name}")
    reset_blender()
    tgt_armature = import_fbx(tgt_path)

    scene = bpy.context.scene
    frame_start = scene.frame_start
    frame_end_blend = frame_start + blend_n  # inclusive boundary

    bone_filter = resolve_bone_filter(args.bones)
    print(
        f"[rebookend] bone filter '{args.bones}' "
        f"({len(bone_filter)} prefix(es); empty = match all)"
    )

    dst_pose = get_authored_pose_at_frame(tgt_armature, frame_end_blend)

    print(f"[rebookend] clearing keyframes in target [{frame_start}, {frame_end_blend}]")
    clear_keyframes_in_range(tgt_armature, frame_start, frame_end_blend, bone_filter)

    print(f"[rebookend] writing {blend_n + 1} blended frames")
    for i in range(blend_n + 1):
        frame = frame_start + i
        alpha = smoothstep(i / blend_n) if blend_n > 0 else 1.0
        blended = blend_pose(src_pose, dst_pose, alpha)
        write_pose_at_frame(tgt_armature, frame, blended, bone_filter)

    # Step 3: export the result. The rest of the timeline (frames > N)
    # retains its original keyframes — the target's authored peak,
    # contact, and recovery are untouched. Rename the action to
    # mixamo.com so the downstream pipeline finds the expected name.
    rename_action_to_mixamo(tgt_armature)
    print(f"[rebookend] exporting: {out_path}")
    export_fbx(out_path)
    print(f"[rebookend] done: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
