"""Reusable mesh + material + scene primitives for headless Blender
authoring scripts.

Every gen_*.py script that produces a `.blend` for a Selva Oscura
static mesh (chapel, castle foundation, crypt, future buildings)
imports these instead of redefining them. Extraction discovered by
factoring gen_chapel_and_descent.py's low-level helpers; the crypt
script now imports these and defines only its own chapel-specific
high-level builders on top.

## Coordinate convention

Blender native: X = lateral, Y = forward-back, Z = up. Every helper
in this module respects that. Any script-specific footprint
convention (chapel façade faces -Y, etc.) is the caller's concern.

## Collection routing

By default every new object is linked into the current collection
(see `current_collection` context manager). If no context is active
the fallback is the scene root. Scripts choose their own collection
names by passing them to `reset_scene()` and using
`with current_collection("<name>"):` to group builders.

## Usage tags

`add_box`, `add_merged_boxes`, `add_mesh_from_pydata` all accept an
optional `usage` argument. This becomes a Blender custom property
on the object, exported as glTF node `extras` and read by the
engine's StaticMeshAssets loader:

- `None` or `"both"` — drawn AND collidable (default)
- `"visual"` — drawn only, skip physics shape build
- `"collision"` — physics only, skip GL upload / draw
"""

import bpy


# ---------------------------------------------------------------------------
# Collection routing
# ---------------------------------------------------------------------------

_current_collection_stack = []


class current_collection:
    """Context manager: while active, every new object created by the
    geometry helpers in this module is linked into `name` instead of
    the scene root. Stacks correctly if used nested.

    Caller is responsible for creating the collection first (done by
    reset_scene when passed a name list, or manually via
    `bpy.data.collections.new(name)` and
    `bpy.context.scene.collection.children.link(coll)`).
    """

    def __init__(self, name):
        self.name = name

    def __enter__(self):
        _current_collection_stack.append(self.name)
        return self

    def __exit__(self, *_exc):
        _current_collection_stack.pop()


def link_to_current(obj):
    """Link `obj` into the collection at the top of the
    current_collection stack, or scene root if the stack is empty.
    primitive_*_add already linked into the scene root, so unlink
    there first to avoid duplicate-link errors.

    Public because scripts sometimes build objects directly via
    bpy.data.objects.new() (custom mesh construction outside the
    add_box / add_merged_boxes / add_mesh_from_pydata path) and
    still need to route them into the active collection."""
    if _current_collection_stack:
        target = bpy.data.collections[_current_collection_stack[-1]]
    else:
        target = bpy.context.scene.collection
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    if obj.name not in target.objects:
        target.objects.link(obj)


# Internal callers in this module still use the underscore name.
_link_to_current = link_to_current


# ---------------------------------------------------------------------------
# Scene reset
# ---------------------------------------------------------------------------

def reset_scene(collection_names):
    """Delete everything in the default scene so we start clean, then
    pre-create the collections named in `collection_names`. Pass the
    list of collection names your script uses (e.g.
    `("chapel_exterior", "chapel_interior")` or
    `("castle_exterior", "castle_interior")`). Order matches the
    conceptual zones the exported .glb files will represent."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    for cname in collection_names:
        new_coll = bpy.data.collections.new(cname)
        bpy.context.scene.collection.children.link(new_coll)


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------

def select_only(obj):
    """Deselect everything, then select+activate `obj`. Required
    before operators like transform_apply or modifier_apply."""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def make_material(name, rgb):
    """Create a Principled BSDF material with `rgb` as its base color
    factor (linear RGB, 0..1 per channel). Returns the material.
    Callers store the returned material in a module-level constant
    and pass it to add_box / add_merged_boxes / add_mesh_from_pydata
    as the `material` argument."""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.85
    return mat


# ---------------------------------------------------------------------------
# Geometry — low-level primitives
# ---------------------------------------------------------------------------

def add_box(name, center, size, material=None, usage=None):
    """Create an axis-aligned box of dimensions `size` (width-X,
    depth-Y, height-Z), centered at `center` (x, y, z). Returns the
    new object. Links into the current_collection() if one is
    active.

    `usage` see module docstring."""
    sx, sy, sz = size
    cx, cy, cz = center

    # primitive_cube_add creates a 2m cube (-1..+1 on each axis).
    # We pass size=1 then resize via scale, applying the scale so
    # the mesh's coordinates reflect the real dimensions.
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(cx, cy, cz))
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (sx, sy, sz)
    select_only(obj)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if material is not None:
        obj.data.materials.append(material)
    if usage is not None:
        obj["usage"] = usage
    _link_to_current(obj)
    return obj


def add_merged_boxes(name, box_specs, material=None, usage=None):
    """Build ONE mesh containing the geometry of every box in
    box_specs (a list of (center, size) tuples). Used to collapse N
    primitives into a single render mesh (one VAO + one Jolt shape
    instead of N). Direct mesh construction via from_pydata is
    ~100x faster than N primitive_cube_add + object.join calls.

    `usage` see module docstring."""
    mesh = bpy.data.meshes.new(name)
    verts = []
    faces = []
    for (cx, cy, cz), (sx, sy, sz) in box_specs:
        hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
        base = len(verts)
        # 8 corners of the AABB.
        verts.extend([
            (cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz),
            (cx + hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz - hz),
            (cx - hx, cy - hy, cz + hz), (cx + hx, cy - hy, cz + hz),
            (cx + hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz + hz),
        ])
        # 6 quad faces (Blender accepts ngons; quads keep indices small).
        faces.extend([
            (base + 0, base + 1, base + 2, base + 3),  # -Z bottom
            (base + 4, base + 7, base + 6, base + 5),  # +Z top
            (base + 0, base + 4, base + 5, base + 1),  # -Y
            (base + 2, base + 6, base + 7, base + 3),  # +Y
            (base + 0, base + 3, base + 7, base + 4),  # -X
            (base + 1, base + 5, base + 6, base + 2),  # +X
        ])
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    _link_to_current(obj)
    if material is not None:
        mesh.materials.append(material)
    if usage is not None:
        obj["usage"] = usage
    return obj


def add_mesh_from_pydata(name, verts, faces, material=None, usage=None):
    """Generic single-mesh builder. Use for collision proxies (e.g.
    a single inclined ramp slab) where add_box geometry isn't a good
    fit. `usage` semantics match add_merged_boxes."""
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    _link_to_current(obj)
    if material is not None:
        mesh.materials.append(material)
    if usage is not None:
        obj["usage"] = usage
    return obj


def boolean_difference(target, cutter, delete_cutter=True):
    """Apply boolean DIFFERENCE: target minus cutter. Optionally
    remove the cutter after the modifier applies."""
    select_only(target)
    mod = target.modifiers.new(name="cut", type="BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    if delete_cutter:
        bpy.data.objects.remove(cutter, do_unlink=True)
