"""Retarget a Mixamo .fbx onto a Tweaked Rigify humanoid rig, export .glb.

    "/c/Program Files/Blender Foundation/Blender 4.2/blender.exe" \
        --background --python \
        games/selva-oscura/scripts/blender/retarget_mixamo.py -- \
        --rig humanoid_male_base4.2.2.blend \
        --src "Standard Idle.fbx" \
        --bmap mixamo_to_humanoid_tweaked_aligned.bmap \
        --posture retarget_posture.json \
        --out humanoid_male/source/clips/standard_idle.glb

Requires the CharMorph + Auto-Rig Pro addons. The .bmap must be staged
in ARP's remap_presets/ folder (we pass only its basename because the
filepath arg is mangled outside the modal file-picker context).
"""

import argparse
import json
import os
import sys

import bpy
from mathutils import Quaternion, Vector

# rig_ops.py sits next to this script; let Blender import it.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig_ops


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--rig", required=True, help="Target rig .blend")
    p.add_argument("--src", required=True, help="Mixamo source .fbx")
    p.add_argument("--bmap", required=True, help="ARP bone-mapping preset path")
    p.add_argument("--posture", required=True, help="Per-frame posture-correction JSON path")
    p.add_argument("--out", required=True, help="Output retargeted .glb path")
    return p.parse_args(argv)


def die(msg: str) -> int:
    sys.stderr.write(f"[retarget_mixamo] {msg}\n")
    return 1


def log(msg: str) -> None:
    sys.stderr.write(f"[retarget_mixamo] {msg}\n")


def find_target_rig(rig_path: str):
    """Open rig .blend, return the DEF- armature."""
    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(rig_path))
    return rig_ops.find_def_armature()


def import_source_fbx(src_path: str):
    """Import a Mixamo .fbx, return (source_rig, action) or (None, None)."""
    pre_objs = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(src_path))
    new_objs = [bpy.data.objects[n] for n in set(bpy.data.objects.keys()) - pre_objs]
    source_rig = next((o for o in new_objs if o.type == "ARMATURE"), None)
    if source_rig is None:
        return None, None
    action = source_rig.animation_data.action if source_rig.animation_data else None
    return source_rig, action


def normalize_mixamo_prefix(source_rig, action) -> None:
    """Rename mixamorig1: -> mixamorig: on bones + fcurve paths so bmap matches.

    Mixamo's re-uploaded characters use mixamorig1: as a disambiguation
    prefix; canonical is mixamorig:.
    """
    OLD, NEW = "mixamorig1:", "mixamorig:"
    for bone in source_rig.data.bones:
        if bone.name.startswith(OLD):
            bone.name = NEW + bone.name[len(OLD):]
    for fc in action.fcurves:
        if OLD in fc.data_path:
            fc.data_path = fc.data_path.replace(OLD, NEW)


def prep_target_rig(target_rig) -> None:
    """Reparent face + strip DEF- constraints to make the target retarget-ready.

    Each DEF-* has a COPY_TRANSFORMS pointing at a tweak/control bone
    that would silently overwrite our direct keyframes; must come off
    before we keyframe.
    """
    rig_ops.reparent_face_to_skull(target_rig)
    for pb in target_rig.pose.bones:
        if pb.name.startswith("DEF-"):
            for c in list(pb.constraints):
                pb.constraints.remove(c)


def run_arp_retarget(scene, source_rig, target_name: str, action, bmap_path: str,
                     frame_start: int, frame_end: int) -> None:
    """Drive ARP's retarget pipeline end-to-end.

    Headless quirks (all needed; the GUI hides them via modal defaults):
      - scene.source_action: required by redefine_rest_pose's poll
      - select_all before copy_bone_rest: it iterates selected_pose_bones
      - freeze_source/target=YES: default NO headlessly; skipping
        _freeze_armature produces spine over-curvature + wrong deltas
    """
    scene.source_rig = source_rig.name
    scene.target_rig = target_name
    scene.source_action = action.name

    bpy.ops.arp.auto_scale()
    bpy.ops.arp.build_bones_list()
    preset_name = os.path.splitext(os.path.basename(bmap_path))[0]
    bpy.ops.arp.import_config_preset(preset_name=preset_name)

    bpy.ops.arp.redefine_rest_pose()
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.arp.copy_bone_rest()
    bpy.ops.arp.save_pose_rest()
    bpy.ops.object.mode_set(mode="OBJECT")

    bpy.ops.arp.retarget(
        frame_start=frame_start, frame_end=frame_end,
        freeze_source="YES", freeze_target="YES",
    )


def collect_bone_fcurves(action):
    return [fc for fc in action.fcurves if fc.data_path.startswith("pose.bones[")]


AXES = {
    "X": Vector((1.0, 0.0, 0.0)),
    "Y": Vector((0.0, 1.0, 0.0)),
    "Z": Vector((0.0, 0.0, 1.0)),
}


def load_posture_corrections(posture_path: str):
    """Parse posture JSON into [(bone_name, Quaternion), ...]."""
    with open(posture_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    out = []
    for entry in cfg.get("corrections", []):
        axis = AXES.get(entry["axis"])
        if axis is None:
            raise ValueError(f"posture: unknown axis {entry['axis']!r} on {entry['bone']!r}")
        out.append((entry["bone"], Quaternion(axis, float(entry["radians"]))))
    return out


def apply_posture_corrections(scene, target_rig, quats, frame_start: int, frame_end: int) -> None:
    """Compose per-bone rotation corrections on top of every retarget frame.

    Each correction is `existing_rotation @ delta_quaternion` keyframed
    per frame. Bones not present on the rig are skipped.
    """
    for frame in range(frame_start, frame_end + 1):
        scene.frame_set(frame)
        for bn, q in quats:
            pb = target_rig.pose.bones.get(bn)
            if pb is None:
                continue
            pb.rotation_quaternion = pb.rotation_quaternion @ q
            pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def prune_stale_actions(keep_action) -> None:
    """Remove every Action except keep_action.

    glTF export writes ALL Actions in bpy.data into the .glb, producing
    multiple .ozz outputs at bake time. export_anim_single_armature
    filters by armature, not by Action -- this is the only way to
    guarantee one clip per file.
    """
    for act in list(bpy.data.actions):
        if act is not keep_action:
            bpy.data.actions.remove(act)


def select_for_export(target_rig) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    target_rig.select_set(True)
    bpy.context.view_layer.objects.active = target_rig
    for child in target_rig.children:
        if child.type == "MESH":
            child.select_set(True)


def export_clip_glb(out_path: str) -> None:
    """Write target rig + its currently-assigned Action as a .glb.

    LOAD-BEARING flags:
      - export_def_bones=False: True orphans face bones from the
        parent chain (elongated-head bug).
      - export_rest_position_armature=True: MUST match
        export_humanoid_glb so ozz inverse-bind matrices agree.
    """
    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=os.path.abspath(out_path),
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_yup=True,
        export_def_bones=False,
        export_rest_position_armature=True,
        export_morph=False,
        export_anim_single_armature=True,
        export_force_sampling=True,
        export_optimize_animation_size=True,
        export_reset_pose_bones=True,
    )


def main() -> int:
    args = parse_args()

    if not hasattr(bpy.context.scene, "source_rig"):
        return die("Auto-Rig Pro addon not enabled")
    for path, label in [(args.rig, "rig"), (args.src, "src"),
                         (args.bmap, "bmap"), (args.posture, "posture")]:
        if not os.path.isfile(path):
            return die(f"--{label} not found: {path}")

    try:
        posture_quats = load_posture_corrections(args.posture)
    except (ValueError, KeyError, json.JSONDecodeError) as e:
        return die(f"posture JSON invalid ({args.posture}): {e}")

    target_rig = find_target_rig(args.rig)
    if target_rig is None:
        return die(f"rig .blend has no armature with DEF- bones: {args.rig}")
    target_name = target_rig.name
    scene = bpy.context.scene  # must re-resolve after open_mainfile

    source_rig, action = import_source_fbx(args.src)
    if source_rig is None:
        return die(f"FBX import produced no armature: {args.src}")
    if action is None:
        return die(f"source FBX has no animation: {args.src}")
    frame_start = int(action.frame_range[0])
    frame_end = int(action.frame_range[1])

    normalize_mixamo_prefix(source_rig, action)
    prep_target_rig(target_rig)
    run_arp_retarget(scene, source_rig, target_name, action, args.bmap, frame_start, frame_end)

    target_rig = bpy.data.objects[target_name]  # re-fetch: arp.retarget may replace it
    out_action = target_rig.animation_data.action if target_rig.animation_data else None
    bone_fcurves = collect_bone_fcurves(out_action) if out_action else []
    if not bone_fcurves:
        return die(
            "FAIL: retarget produced no bone fcurves "
            "(possible slot-type bug on Blender 5.x; verify 4.2 is in use)")

    apply_posture_corrections(scene, target_rig, posture_quats, frame_start, frame_end)
    prune_stale_actions(out_action)
    select_for_export(target_rig)
    export_clip_glb(args.out)

    log(f"wrote {args.out} (frames {frame_start}..{frame_end}, {len(bone_fcurves)} bone fcurves)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
