"""Bake a Mixamo .fbx onto the humanoid_male character, export .glb.

    "/c/Program Files/Blender Foundation/Blender 4.2/blender.exe" \
        --background --python \
        games/selva-oscura/scripts/blender/bake_clip.py -- \
        --char humanoid_male.blend \
        --src "Jog Forward.fbx" \
        --out humanoid_male/source/clips/jogging.glb

Both the character rig and the Mixamo source use the canonical Mixamo
skeleton (mixamorig:* prefix, 52 bones). MPFB2's map_mixamo operator
binds the source animation onto the character via per-bone copy
constraints; we bake those constraints to keyframes then strip them.
"""

import argparse
import os
import sys

import bpy


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--char", required=True, help="humanoid_male.blend authoring file")
    p.add_argument("--src", required=True, help="Mixamo source .fbx")
    p.add_argument("--out", required=True, help="Output .glb path")
    return p.parse_args(argv)


def die(msg: str) -> int:
    sys.stderr.write(f"[bake_clip] {msg}\n")
    return 1


def main() -> int:
    args = parse_args()
    bpy.ops.preferences.addon_enable(module="bl_ext.user_default.mpfb")

    for path, label in [(args.char, "char"), (args.src, "src")]:
        if not os.path.isfile(path):
            return die(f"--{label} not found: {path}")

    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(args.char))
    char_rig = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    if char_rig is None:
        return die(f"character .blend missing armature: {args.char}")
    # Pick the mesh that's a child of the armature so stray scene meshes
    # (default Cube etc) can't impersonate the basemesh.
    basemesh = next((c for c in char_rig.children if c.type == "MESH"), None)
    if basemesh is None:
        return die(f"character .blend armature has no skinned mesh child: {args.char}")

    pre_objs = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(args.src))
    src_rig = next((bpy.data.objects[n] for n in set(bpy.data.objects.keys()) - pre_objs
                    if bpy.data.objects[n].type == "ARMATURE"), None)
    if src_rig is None or src_rig.animation_data is None or src_rig.animation_data.action is None:
        return die(f"source FBX has no rigged action: {args.src}")
    src_action = src_rig.animation_data.action
    frame_start = int(src_action.frame_range[0])
    frame_end = int(src_action.frame_range[1])

    bpy.ops.object.select_all(action="DESELECT")
    src_rig.select_set(True)
    char_rig.select_set(True)
    bpy.context.view_layer.objects.active = char_rig
    bpy.ops.mpfb.map_mixamo()

    bpy.ops.object.select_all(action="DESELECT")
    char_rig.select_set(True)
    bpy.context.view_layer.objects.active = char_rig
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.nla.bake(
        frame_start=frame_start,
        frame_end=frame_end,
        only_selected=True,
        visual_keying=True,
        clear_constraints=True,
        clear_parents=False,
        bake_types={"POSE"},
    )
    bpy.ops.object.mode_set(mode="OBJECT")

    bpy.data.objects.remove(src_rig, do_unlink=True)

    baked_action = char_rig.animation_data.action
    for act in list(bpy.data.actions):
        if act is not baked_action:
            bpy.data.actions.remove(act)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    bpy.ops.object.select_all(action="DESELECT")
    basemesh.select_set(True)
    char_rig.select_set(True)
    bpy.context.view_layer.objects.active = char_rig
    bpy.ops.export_scene.gltf(
        filepath=os.path.abspath(args.out),
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_yup=True,
        export_def_bones=False,
        export_morph=False,
        export_anim_single_armature=True,
        export_force_sampling=True,
    )
    sys.stderr.write(
        f"[bake_clip] wrote {args.out} (frames {frame_start}..{frame_end})\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
