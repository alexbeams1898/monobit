"""Export crypt_foundation.blend to per-region .glb files.

The source .blend (tracked in git) contains TWO collections authored
by gen_crypt_foundation.py:
  - chapel_exterior  -> chapel_exterior.glb (facade, plinth, roof, walls, etc.)
  - chapel_interior  -> chapel_interior.glb (interior floor + descent staircase)

This script opens the .blend and exports each collection to its own
.glb file in assets/world/static_meshes/, ready for the engine's
StaticMeshAssets loader. Per-region static meshes lets each region.json
declare ONLY the meshes its region owns; chapel_exterior and chapel_
interior become independent assets even though they share the JOINT
dictionary's cross-zone constants in the generator.

Run via:
    bash games/selva-oscura/scripts/blender/gen_crypt_export.sh

Or directly:
    "/c/Program Files/Blender Foundation/Blender 5.1/blender.exe" \\
        --background \\
        games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend \\
        --python games/selva-oscura/scripts/blender/gen_crypt_export.py
"""

import os
import bpy


REPO_REL_INPUT = "games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend"
REPO_REL_OUTPUT_DIR = "games/selva-oscura/assets/world/static_meshes"

# Match COLLECTION_NAMES in gen_crypt_foundation.py. Each name maps to
# a .glb output file of the same name + ".glb".
EXPORT_COLLECTIONS = ("chapel_exterior", "chapel_interior")


def select_collection_only(collection_name):
    """Deselect everything, then select every object in `collection_name`
    (and only that collection). bpy.ops.export_scene.gltf with
    use_selection=True will export exactly the selection."""
    bpy.ops.object.select_all(action="DESELECT")
    coll = bpy.data.collections.get(collection_name)
    if coll is None:
        return 0
    n_selected = 0
    for obj in coll.objects:
        obj.select_set(True)
        n_selected += 1
    return n_selected


def export_collection_glb(collection_name, output_path):
    n = select_collection_only(collection_name)
    if n == 0:
        print(f"[gen_crypt_export] WARNING: collection '{collection_name}' is empty -- skipping")
        return
    bpy.ops.export_scene.gltf(
        filepath=output_path,
        export_format="GLB",
        use_selection=True,        # export only the selected (collection) objects
        export_apply=True,         # apply modifiers
        export_yup=True,           # +Y is up in glTF convention
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_cameras=False,
        export_lights=False,
        export_animations=False,   # static mesh, no anims
        # Propagate Blender custom properties (obj["usage"] = ...) into
        # glTF node `extras`. The C++ loader (StaticMeshAssets.cpp)
        # reads `extras.usage` to decide whether each primitive is
        # drawn, collided with, or both. Without this, custom
        # properties are silently dropped on export.
        export_extras=True,
    )
    print(f"[gen_crypt_export] wrote {output_path} ({n} objects from '{collection_name}')")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", "..", "..", ".."))
    output_dir = os.path.join(repo_root, REPO_REL_OUTPUT_DIR)
    os.makedirs(output_dir, exist_ok=True)

    # If invoked without the .blend on the cmdline (i.e. directly via
    # --python without a file arg), open it explicitly.
    input_path = os.path.join(repo_root, REPO_REL_INPUT)
    if not bpy.data.objects:
        bpy.ops.wm.open_mainfile(filepath=input_path)

    for cname in EXPORT_COLLECTIONS:
        output_path = os.path.join(output_dir, f"{cname}.glb")
        export_collection_glb(cname, output_path)


if __name__ == "__main__":
    main()
