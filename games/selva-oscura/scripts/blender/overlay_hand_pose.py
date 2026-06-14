"""
Overlay a hand's pose from a SOURCE Mixamo FBX onto every frame of a
TARGET Mixamo FBX, exporting a new FBX. Used to produce an "armed
relaxed idle" by taking the standard_idle body and curling the right
hand from sword_and_shield_idle_4 onto it -- since the Pro Sword and
Shield Pack doesn't ship a true relaxed-armed idle.

Both source and target must share the same skeleton (Mixamo
mixamorig:* bones). The overlay is a hard write (not a blend): every
frame of the target gets the source's rotation_quaternion and
location for the chosen bones, overwriting whatever the target
authored for those bones.

Usage:
    blender --background --python overlay_hand_pose.py -- \\
        --source "<source.fbx>" --source-time <seconds> \\
        --target "<target.fbx>" \\
        --output "<output.fbx>" \\
        [--bones right_hand_grip]

Bone groups (preset names):
    right_hand_grip (default) -- mixamorig:RightHand + its 15 finger
        bones (Thumb1/2/3, Index1/2/3, Middle1/2/3, Ring1/2/3,
        Pinky1/2/3). Right-hand-only grip overlay.
    right_hand_only -- just mixamorig:RightHand (no finger bones).
    left_hand_grip -- the mirror set for the left hand.

You can also pass a comma-separated list of bone-name prefixes
instead of a preset name.
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector


def parse_args() -> argparse.Namespace:
    if "--" in sys.argv:
        argv = sys.argv[sys.argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser(description="Overlay a hand pose from one Mixamo FBX onto every frame of another.")
    parser.add_argument(
        "--source", required=True, help="Source FBX whose pose at --source-time we copy from."
    )
    parser.add_argument(
        "--source-time",
        type=float,
        default=0.0,
        help="Time in source clip (seconds) whose pose is the overlay. Default 0 = first frame.",
    )
    parser.add_argument(
        "--target", required=True, help="Target FBX. Every frame gets the source bones written onto it."
    )
    parser.add_argument(
        "--output", required=True, help="Output FBX path. Parent directory will be created if missing."
    )
    parser.add_argument(
        "--bones",
        default="right_hand_grip",
        help="Bone preset name (right_hand_grip / right_hand_only / left_hand_grip), or a comma-separated list of bone-name prefixes.",
    )
    parser.add_argument(
        "--right-arm-lift-deg",
        type=float,
        default=0.0,
        help="Lift mixamorig:RightArm (upper arm) forward at the shoulder. Positive = weapon hand lifts slightly in front of the leg without swinging sideways.",
    )
    parser.add_argument(
        "--right-arm-out-deg",
        type=float,
        default=0.0,
        help="Swing mixamorig:RightArm sideways (away from torso). Positive = arm moves to the side. Combine with --right-arm-lift-deg if a small lateral component is also needed.",
    )
    parser.add_argument(
        "--right-forearm-bend-deg",
        type=float,
        default=0.0,
        help="Bend mixamorig:RightForeArm at the elbow. Positive = forearm angles forward/out from the upper arm.",
    )
    parser.add_argument(
        "--right-hand-tilt-x-deg",
        type=float,
        default=0.0,
        help="Tilt mixamorig:RightHand around its local X axis (one component of the wrist hinge).",
    )
    parser.add_argument(
        "--right-hand-tilt-y-deg",
        type=float,
        default=0.0,
        help="Tilt mixamorig:RightHand around its local Y axis (the second component of the wrist hinge).",
    )
    return parser.parse_args(argv)


BONE_GROUPS: dict[str, list[str]] = {
    "right_hand_only": [
        "mixamorig:RightHand",
    ],
    "right_hand_grip": [
        "mixamorig:RightHand",
        "mixamorig:RightHandThumb",
        "mixamorig:RightHandIndex",
        "mixamorig:RightHandMiddle",
        "mixamorig:RightHandRing",
        "mixamorig:RightHandPinky",
    ],
    "right_arm_chain": [
        "mixamorig:RightShoulder",
        "mixamorig:RightArm",
        "mixamorig:RightForeArm",
        "mixamorig:RightHand",
        "mixamorig:RightHandThumb",
        "mixamorig:RightHandIndex",
        "mixamorig:RightHandMiddle",
        "mixamorig:RightHandRing",
        "mixamorig:RightHandPinky",
    ],
    "left_hand_grip": [
        "mixamorig:LeftHand",
        "mixamorig:LeftHandThumb",
        "mixamorig:LeftHandIndex",
        "mixamorig:LeftHandMiddle",
        "mixamorig:LeftHandRing",
        "mixamorig:LeftHandPinky",
    ],
}


def resolve_bone_filter(spec: str) -> list[str]:
    if spec in BONE_GROUPS:
        return BONE_GROUPS[spec]
    return [s.strip() for s in spec.split(",") if s.strip()]


def bone_matches(bone_name: str, prefixes: list[str]) -> bool:
    if not prefixes:
        return False
    return any(bone_name.startswith(p) for p in prefixes)


def reset_blender() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def iter_action_fcurves(action: bpy.types.Action):
    """Yield every f-curve in a Blender 5.x layered Action."""
    for layer in action.layers:
        for strip in layer.strips:
            if strip.type != "KEYFRAME":
                continue
            for cb in strip.channelbags:
                for fcurve in cb.fcurves:
                    yield fcurve


def import_fbx(path: Path) -> bpy.types.Object:
    bpy.ops.import_scene.fbx(filepath=str(path))
    armature = next(
        (obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None
    )
    if armature is None:
        raise RuntimeError(f"No armature in {path}")
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


def sample_pose_at_time(armature: bpy.types.Object, t_seconds: float, bone_filter: list[str]) -> dict[str, dict]:
    """Sample rotation/location for filter-matched bones at clip-time t_seconds."""
    scene = bpy.context.scene
    fps = scene.render.fps / scene.render.fps_base
    target_frame = scene.frame_start + int(round(t_seconds * fps))
    target_frame = max(scene.frame_start, min(scene.frame_end, target_frame))
    scene.frame_set(target_frame)
    pose_data: dict[str, dict] = {}
    for pbone in armature.pose.bones:
        if not bone_matches(pbone.name, bone_filter):
            continue
        pose_data[pbone.name] = {
            "rot": pbone.rotation_quaternion.copy(),
            "loc": pbone.location.copy(),
        }
    return pose_data


def clear_keyframes_for_bones(armature: bpy.types.Object, bone_filter: list[str]) -> None:
    """Remove entire f-curves for matched bones from the action. We're
    overwriting every frame with the overlay pose, so removing each
    f-curve is cleaner than removing keyframes one-by-one (Blender 5.x
    raises 'Keyframe not in F-Curve' under per-key removal). The
    subsequent keyframe_insert calls recreate the f-curves with the
    overlay pose's values."""
    if armature.animation_data is None or armature.animation_data.action is None:
        print("[overlay] WARNING: target has no animation_data.action; nothing to clear")
        return
    action = armature.animation_data.action
    total_fcurves = 0
    total_matched = 0
    total_removed = 0
    sample_paths = []
    for layer in action.layers:
        for strip in layer.strips:
            if strip.type != "KEYFRAME":
                continue
            for cb in strip.channelbags:
                fcurves_to_remove = []
                for fcurve in cb.fcurves:
                    total_fcurves += 1
                    path = fcurve.data_path
                    if len(sample_paths) < 5:
                        sample_paths.append(path)
                    bone_name = ""
                    if 'pose.bones["' in path:
                        start = path.index('pose.bones["') + len('pose.bones["')
                        end = path.index('"]', start)
                        bone_name = path[start:end]
                    if bone_matches(bone_name, bone_filter):
                        total_matched += 1
                        fcurves_to_remove.append(fcurve)
                for fcurve in fcurves_to_remove:
                    cb.fcurves.remove(fcurve)
                    total_removed += 1
    print(
        f"[overlay] f-curve scan: total={total_fcurves} matched={total_matched} removed={total_removed}"
    )
    if total_fcurves == 0:
        print("[overlay] WARNING: no f-curves found in action layers. Sample paths: (none)")
    elif total_matched == 0:
        print(
            f"[overlay] WARNING: 0 f-curves matched the filter. Sample f-curve paths: {sample_paths}"
        )


def write_pose_every_frame(
    armature: bpy.types.Object,
    pose_data: dict[str, dict],
) -> None:
    """Write the same pose at every frame in [frame_start, frame_end].
    Bones not in pose_data are skipped (their authored keyframes already
    cleared if they matched the filter; if they didn't match, we leave
    them entirely alone)."""
    scene = bpy.context.scene
    for frame in range(scene.frame_start, scene.frame_end + 1):
        scene.frame_set(frame)
        for bone_name, data in pose_data.items():
            pbone = armature.pose.bones.get(bone_name)
            if pbone is None:
                continue
            pbone.rotation_quaternion = data["rot"]
            pbone.location = data["loc"]
            pbone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
            pbone.keyframe_insert(data_path="location", frame=frame)


def apply_rotation_correction_every_frame(
    armature: bpy.types.Object,
    corrections: list[tuple[str, str, float]],
) -> None:
    """Apply a per-bone local-axis rotation correction at every frame in
    [frame_start, frame_end]. `corrections` is a list of (bone_name,
    axis, degrees) tuples; axis is one of "X", "Y", "Z". The correction
    composes ON TOP OF the bone's existing pose at that frame (read
    pose, multiply by correction quat, write back). Used to angle a
    relaxed arm outward without losing the source clip's authored
    micro-motion (breathing, sway). Bones not present in the armature
    are silently skipped."""
    if not corrections:
        return
    scene = bpy.context.scene
    axis_vectors = {
        "X": Vector((1.0, 0.0, 0.0)),
        "Y": Vector((0.0, 1.0, 0.0)),
        "Z": Vector((0.0, 0.0, 1.0)),
    }
    correction_quats = []
    for bone_name, axis, degrees in corrections:
        if axis not in axis_vectors:
            print(
                f"[overlay] WARNING: unknown axis '{axis}' for {bone_name}; skipping",
                file=sys.stderr,
            )
            continue
        if abs(degrees) < 1e-6:
            continue
        radians = degrees * 3.141592653589793 / 180.0
        q = Quaternion(axis_vectors[axis], radians)
        correction_quats.append((bone_name, q))
    if not correction_quats:
        return
    print(
        f"[overlay] applying {len(correction_quats)} arm-rotation correction(s) "
        f"across {scene.frame_end - scene.frame_start + 1} frames"
    )
    for frame in range(scene.frame_start, scene.frame_end + 1):
        scene.frame_set(frame)
        for bone_name, q in correction_quats:
            pbone = armature.pose.bones.get(bone_name)
            if pbone is None:
                continue
            pbone.rotation_quaternion = pbone.rotation_quaternion @ q
            pbone.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def rename_action_to_mixamo(armature: bpy.types.Object) -> None:
    if armature.animation_data is not None and armature.animation_data.action is not None:
        armature.animation_data.action.name = "mixamo.com"
    bpy.context.scene.name = "mixamo.com"


def export_fbx(path: Path) -> None:
    bpy.ops.export_scene.fbx(
        filepath=str(path),
        use_selection=False,
        bake_anim=True,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        add_leaf_bones=False,
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

    bone_filter = resolve_bone_filter(args.bones)
    print(
        f"[overlay] bone filter '{args.bones}' "
        f"({len(bone_filter)} prefix(es); {bone_filter})"
    )

    # Step 1: sample source pose at the chosen time for matched bones only.
    print(f"[overlay] sampling source: {src_path.name} @ t={args.source_time:.3f}s")
    reset_blender()
    src_armature = import_fbx(src_path)
    src_pose = sample_pose_at_time(src_armature, args.source_time, bone_filter)
    if not src_pose:
        print(
            f"ERROR: no bones matched filter on source armature. "
            f"Check --bones spec.",
            file=sys.stderr,
        )
        return 2
    print(f"[overlay] captured {len(src_pose)} bones from source")

    # Step 2: load target fresh; clear target's matched-bone keyframes;
    # write the source pose at every frame.
    print(f"[overlay] loading target: {tgt_path.name}")
    reset_blender()
    tgt_armature = import_fbx(tgt_path)
    scene = bpy.context.scene
    print(
        f"[overlay] target timeline: frames {scene.frame_start}..{scene.frame_end} "
        f"({scene.frame_end - scene.frame_start + 1} frames)"
    )
    print(f"[overlay] clearing matched-bone keyframes on target")
    clear_keyframes_for_bones(tgt_armature, bone_filter)
    print(f"[overlay] writing source pose to every frame")
    write_pose_every_frame(tgt_armature, src_pose)

    # Optional right-arm rotation corrections. Applied AFTER the hand
    # overlay so the corrected arm carries the curled hand with it.
    # Mixamo bone-local axis convention (verified by trial):
    #   RightArm: X = lift forward (shoulder hinge), Z = swing sideways.
    #   RightForeArm: X = bend at elbow.
    arm_corrections = [
        ("mixamorig:RightArm", "X", -args.right_arm_lift_deg),
        ("mixamorig:RightArm", "Z", -args.right_arm_out_deg),
        ("mixamorig:RightForeArm", "X", -args.right_forearm_bend_deg),
        ("mixamorig:RightHand", "X", -args.right_hand_tilt_x_deg),
        ("mixamorig:RightHand", "Y", args.right_hand_tilt_y_deg),
    ]
    apply_rotation_correction_every_frame(tgt_armature, arm_corrections)

    rename_action_to_mixamo(tgt_armature)
    print(f"[overlay] exporting: {out_path}")
    export_fbx(out_path)
    print(f"[overlay] done: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
