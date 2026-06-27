"""Build a fresh humanoid rig via CharMorph -> Tweaked Rigify, save as
.blend at the given output path. Headless, deterministic: same inputs
produce the same .blend every run.

Invoke from repo root:

    "/c/Program Files/Blender Foundation/Blender 4.2/blender.exe" \
        --background --python \
        games/selva-oscura/scripts/blender/setup_humanoid_rig.py -- \
        --base mb_male --out /tmp/humanoid_male_rigged.blend

--base is the CharMorph library character key ("mb_male" / "mb_female").
--out is the .blend save path. Existing files at that path are overwritten.

The resulting .blend contains the CharMorph mesh + Tweaked Rigify
armature ready for either FBX/glTF export (engine bake input) or as a
target for retarget operations (see retarget_mixamo.py).

Requires CharMorph addon enabled in this Blender install. The CharMorph
data library must be present at the addon's data/ folder (one-time
setup; see addon docs).
"""

import argparse
import os
import sys

import bpy


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    p = argparse.ArgumentParser()
    p.add_argument("--base", required=True, help="CharMorph base model key (e.g. mb_male)")
    p.add_argument("--out", required=True, help="Output .blend path")
    return p.parse_args(argv)


def main() -> int:
    args = parse_args()

    if not hasattr(bpy.context.window_manager, "charmorph_ui"):
        sys.stderr.write(
            "[setup_humanoid_rig] CharMorph addon not enabled -- "
            "enable it in Blender's preferences first\n")
        return 1

    # Clear the default startup scene (cube, camera, light) without
    # resetting addon state. read_factory_settings(use_empty=True)
    # unregisters CharMorph + ARP because they're scene-property-bound;
    # deleting the default objects manually preserves addon state.
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    ui = bpy.context.window_manager.charmorph_ui
    ui.base_model = args.base

    bpy.ops.charmorph.import_char()
    if not any(o.name.startswith("mb_") for o in bpy.data.objects):
        sys.stderr.write(f"[setup_humanoid_rig] import_char produced no mesh for base={args.base}\n")
        return 1

    ui.rig = "tweaked"
    ui.fin_rig = True

    bpy.ops.charmorph.finalize()
    rig = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    if rig is None:
        sys.stderr.write("[setup_humanoid_rig] finalize produced no armature\n")
        return 1

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    sys.stderr.write(f"[setup_humanoid_rig] wrote {args.out} (rig={rig.name})\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
