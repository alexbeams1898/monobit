"""Re-export a .glb through Blender's Khronos glTF I/O.

UniGLTF-exported files (Kenney's Nature Kit, etc.) ship in a form
our cgltf-based static-mesh loader rejects with
cgltf_result_invalid_gltf. Stripping extensions alone isn't enough;
UniGLTF emits other quirks (number formats, accessor layouts) that
cgltf doesn't tolerate.

Re-exporting via Blender's Khronos Blender glTF I/O exporter produces
a file in the same dialect as our other working assets
(games/selva-oscura/assets/world/static_meshes/*.glb -- all
generator='Khronos glTF Blender I/O').

Run via:
  blender --background --python reexport_glb_via_blender.py -- <in.glb> <out.glb>

Multiple in/out pairs can be passed; they're processed left-to-right.
"""

import sys

try:
    import bpy
except ImportError:
    print('Run this script inside Blender: blender --background --python <this>', file=sys.stderr)
    sys.exit(1)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def reexport_one(in_path, out_path):
    reset_scene()
    bpy.ops.import_scene.gltf(filepath=in_path)
    bpy.ops.export_scene.gltf(
        filepath=out_path,
        export_format='GLB',
        export_apply=True,
    )
    print(f'Re-exported {in_path} -> {out_path}')


def main():
    # bpy strips the script's own args; user args follow `--` on the
    # command line.
    if '--' in sys.argv:
        user_args = sys.argv[sys.argv.index('--') + 1:]
    else:
        user_args = []
    if len(user_args) < 2 or len(user_args) % 2 != 0:
        print('Usage: ... -- <in.glb> <out.glb> [<in2.glb> <out2.glb> ...]', file=sys.stderr)
        sys.exit(2)
    for i in range(0, len(user_args), 2):
        reexport_one(user_args[i], user_args[i + 1])


if __name__ == '__main__':
    main()
