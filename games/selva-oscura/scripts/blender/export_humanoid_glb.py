"""Export the humanoid rig + mesh from a saved .blend as glTF binary (.glb).

    "/c/Program Files/Blender Foundation/Blender 4.2/blender.exe" \
        --background --python export_humanoid_glb.py -- \
        --rig humanoid_male_base4.2.2.blend \
        --out humanoid_male/source/rig/humanoid.glb

The exported .glb pairs with retarget_mixamo.py's clip output: both
come from the same source .blend so the baked skeleton.ozz and
per-clip .ozz files share joint order + count.
"""

import argparse
import os
import sys

import bpy

# rig_ops.py sits next to this script; let Blender import it.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig_ops


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--rig", required=True, help="Source .blend with rig + mesh")
    p.add_argument("--out", required=True, help="Output .glb path")
    return p.parse_args(argv)


def die(msg: str) -> int:
    sys.stderr.write(f"[export_humanoid_glb] {msg}\n")
    return 1


def remove_stray_armatures(target_rig) -> None:
    """Delete other armatures + their children so the .glb is single-rig.

    Stray Mixamo source rigs from prior retargets or the metarig would
    otherwise leak into the export.
    """
    to_delete = [o for o in bpy.data.objects
                 if o.type == "ARMATURE" and o is not target_rig]
    to_delete += [c for arm in to_delete for c in arm.children]
    for obj in to_delete:
        bpy.data.objects.remove(obj, do_unlink=True)


def export_rig_glb(out_path: str) -> None:
    """Write the rig + mesh as a static .glb (no animation).

    LOAD-BEARING flags:
      - export_def_bones=False: True orphans face bones from the
        parent chain (elongated-head bug).
      - export_rest_position_armature=True: MUST match the clip export
        so ozz inverse-bind matrices agree.
    """
    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=os.path.abspath(out_path),
        export_format="GLB",
        use_visible=True,
        export_apply=True,
        export_yup=True,
        export_def_bones=False,
        export_rest_position_armature=True,
        export_morph=False,
        export_animations=False,
    )


def main() -> int:
    args = parse_args()
    if not os.path.isfile(args.rig):
        return die(f"--rig not found: {args.rig}")

    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(args.rig))
    target_rig = rig_ops.find_def_armature()
    if target_rig is None:
        return die(f"no DEF- armature in {args.rig}")

    remove_stray_armatures(target_rig)
    rig_ops.reparent_face_to_skull(target_rig)
    export_rig_glb(args.out)

    sys.stderr.write(f"[export_humanoid_glb] wrote {args.out}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
