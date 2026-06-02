"""Generate the foundational geometry of the colle-plateau crypt.

Produces a fresh `.blend` file with the structural skeleton of a
Cappella-Madonna-di-Vitaleta-style chapel: body, plinth, pediment,
roof, semicircular rear apse, architraved door cutout, oculus, and
two-cell rear bell-arch. All elements are separate named objects,
ready to refine by hand in Blender.

The script is parametric — every dimension is named at the top of
the module and can be changed before regenerating. No published
official measurements exist for Vitaleta online; values were
photogrammetrically estimated from public photos, with the woman
in the foreground (~4m behind the front face) as the human-scale
anchor. Expect ~15% accuracy. Adjust if needed.

Coordinate convention (Blender native): X = lateral, Y = forward-back,
Z = up. The chapel's footprint is centered on (0, 0) at ground (Z=0).
The façade faces -Y (front of chapel toward -Y). The apse extends in
+Y (rear).

Run from repo root via:

    "/c/Program Files/Blender Foundation/Blender 5.1/blender.exe" \\
        --background \\
        --python games/selva-oscura/scripts/blender/gen_crypt_foundation.py

Or use gen_crypt_foundation.sh (companion shell launcher).

Output: games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend
"""

import math
import os
import sys

import bpy
import bmesh


# ---------------------------------------------------------------------------
# Spec — change here, regenerate to see the difference
# ---------------------------------------------------------------------------

# Body footprint (exterior).
BODY_WIDTH = 6.0            # X dimension, façade-to-façade across short wall
BODY_LENGTH = 8.0           # Y dimension, front-to-back along long wall
BODY_HEIGHT_TO_CORNICE = 6.7  # Z dimension, ground to base of pediment

# Wall thickness (interior is hollow). Stone masonry of this typology is
# ~50-60cm in the historical record.
WALL_THICKNESS = 0.60

# Pediment / roof (gable).
PEDIMENT_HEIGHT = 1.3       # cornice to apex; gives ~24° pitch on a 6m-wide façade
ROOF_OVERHANG = 0.25        # eave projection on long sides

# Plinth (zoccolo).
PLINTH_HEIGHT = 0.30
PLINTH_PROJECTION = 0.05    # how far it extrudes outward from the wall plane

# Cornice (single fascia under the pediment).
CORNICE_HEIGHT = 0.20
CORNICE_PROJECTION = 0.12

# Corner pilasters (lesene). Vitaleta's are very subtle, almost flush.
PILASTER_WIDTH = 0.30
PILASTER_PROJECTION = 0.02

# Door (architraved, NOT arched — confirmed for Vitaleta).
DOOR_WIDTH = 1.0
DOOR_HEIGHT = 2.15

# Debug: when True, seal the door with a solid slab so the interior
# has no opening. Used to isolate whether the interior light comes
# from the door (test goes pitch black) or from elsewhere (test stays
# lit → another leak to find).
DEBUG_SEAL_DOOR = False

# Oculus (rosone) — small circular window above the door.
OCULUS_RADIUS = 0.375       # diameter 0.75m
OCULUS_CENTER_Z = 5.0       # height above ground

# Apse (semicircular projection on rear wall).
APSE_RADIUS = 1.8
APSE_HEIGHT = BODY_HEIGHT_TO_CORNICE  # matches body up to the cornice line

# Cross at the apex.
CROSS_HEIGHT = 0.7
CROSS_ARM_LENGTH = 0.25
CROSS_THICKNESS = 0.05

# Mesh resolution.
APSE_SEGMENTS = 24          # circle segments for the apse curve
OCULUS_SEGMENTS = 24

# Output.
OUTPUT_REL = "games/selva-oscura/assets/world/static_meshes/source/crypt_foundation.blend"

# ---------------------------------------------------------------------------
# Stair-descent spec (San Miniato flanking pair + single carved corridor
# down to Limbo platform)
# ---------------------------------------------------------------------------

# Step proportions — Souls/ER convention (~25° slope). Rise/tread
# slightly gentler than real-world residential code so the player
# moves smoothly. Visible mesh has discrete steps; collision is a
# single continuous ramp (see populateCryptDescent in Collision.cpp)
# matching the From Software approach — visible-step mesh over
# ramped collision avoids per-step snag/stutter.
STAIR_RISE = 0.16           # vertical per step
STAIR_TREAD = 0.35          # horizontal per step
STAIR_STEP_DEPTH = 0.08     # visual step slab thickness

# Single flight spanning the full chapel interior width. Descends in
# +Y direction (toward apse / -Z world / toward the sun). The
# previous two-flank layout has been collapsed into one wide flight.
SINGLE_FLIGHT_HALF_WIDTH = 2.40   # half-width of stair, hole, plinth side strip, and back-wall tunnel. MUST stay in sync with kSingleFlightHalfWidth in games/selva-oscura/include/world/CryptLayout.h. Matches corridor_half_w = chapel interior half-width (BODY_WIDTH/2 - WALL_THICKNESS = 2.4).
UPPER_FLIGHT_STEP_COUNT = 9       # 9 * 0.35m tread = 3.15m run, 9 * 0.16m rise = 1.44m drop
UPPER_FLIGHT_TOP_Y = 0.0          # chapel-local Y of top step (chapel center)
# Sign convention for the flight direction. +1 = descends in +Y
# (toward apse = toward sun in world). -1 = toward door.
DESCENT_FORWARD_SIGN = 1

# Continuous descent: one uniform staircase from the bottom of the
# upper flight all the way down to the Limbo platform. Same step
# proportions as the upper flight so the descent feels smooth and
# steady from the chapel down — no transitions, no platforms, no
# variable-slope ramps. The player walks 0.16m down per 0.35m forward
# all the way to Limbo.
#
# Real-physics doctrine: uniform stairs mean the capsule's step-down
# behavior is identical at every step — no jarring drops at landing
# transitions, no slopes too steep for CharacterVirtual to handle.
CONTINUOUS_DESCENT_STEP_COUNT = 400  # 400 * 0.16m = 64m total drop from chapel floor; matches the previous corridor's vertical extent (~60m to Limbo)

# Hole through chapel floor. Spans the full chapel interior width
# exactly (X = chapel interior X span), length = full flight run.
HOLE_X_PADDING = 0.0
HOLE_Y_PADDING = 0.0

# Landing where the two flights meet (single Z, single Y).
LANDING_WIDTH_X = 3.0       # spans both flights + center wall gap
LANDING_DEPTH_Y = 2.0       # in Y direction (forward of where flights end)
LANDING_HEIGHT_Y = 3.0      # clearance above landing surface (corridor height)

# Corridor — single tunnel descending straight from landing to Limbo.
CORRIDOR_WIDTH = 2.5
CORRIDOR_HEIGHT = 3.0
# 4 long ramp segments + 3 landings (where the alcoves are).
CORRIDOR_SEGMENT_COUNT = 4
CORRIDOR_SEGMENT_HORIZONTAL_RUN = 12.0  # per ramp, in Y (forward)
CORRIDOR_SEGMENT_VERTICAL_DROP = 15.2   # per ramp, in -Z
CORRIDOR_LANDING_LENGTH = 2.0           # flat landing between ramps

# Alcove (per landing) — recessed pocket on one side wall.
ALCOVE_DEPTH = 1.5
ALCOVE_WIDTH = 1.8
ALCOVE_HEIGHT = 2.4

# Limbo (the first circle of Hell) is its own terrain region — see
# assets/world/terrain/config.json: limbo. The continuous descent
# terminates at the bottom of its final step; the Limbo terrain
# picks up from that point in world space via its y_offset.


# ---------------------------------------------------------------------------
# JOINT geometry — chapel ↔ descent transition spec
# ---------------------------------------------------------------------------
#
# Every primitive that sits at the chapel/descent junction derives its
# dimensions from this dict. ONE source of truth: corridor cross-section
# defined here is reused by the back-wall tunnel cutout, the apse
# doorway cutout, the corridor walls/ceiling, and the plinth back-strip
# tunnel opening.
#
# Real-physics doctrine + diamond-foundation rule: no primitive surface
# is allowed within Z_FIGHT_SAFETY of another primitive's surface
# without explicit allowlist. The post-build audit (audit_chapel_sources.py)
# enforces this and fails the export if two non-allowlisted surfaces are
# closer than Z_FIGHT_SAFETY but not exactly coincident.
JOINT = {
    # Corridor cross-section — INTERIOR space where the player walks.
    # Used by EVERY primitive that defines the corridor: back-wall
    # tunnel, apse doorway, corridor walls, corridor ceiling, plinth
    # tunnel opening.
    "corridor_half_w":            2.40,  # X half-extent of walkable corridor interior — matches chapel INTERIOR half-width (BODY_WIDTH/2 - WALL_THICKNESS = 3.0 - 0.6 = 2.4) so the chapel back wall opens fully into the corridor; chapel side walls continue as the corridor's outer walls. No L-shape.
    "corridor_wall_thickness":    0.60,  # X thickness of the corridor's side walls — matches WALL_THICKNESS so corridor walls = continuation of chapel side walls (X ±3.0 outer face).
    "corridor_ceiling_clearance": 4.00,  # Y between step-top and ceiling-underside
    "corridor_ceiling_thickness": 0.30,  # Z thickness of the ceiling slab

    # Apse geometry. The apse half-cylinder sits BEHIND the chapel back
    # wall. apse_back_wall_inset pushes the cylinder INTO the back-wall
    # plane so the apse half-cut face is buried inside back-wall
    # material rather than coplanar with the wall's outer face.
    "apse_radius":          1.80,
    "apse_back_wall_inset": 0.05,

    # Standard safety gap. Every "two primitives near each other but
    # not designed to touch" pair is separated by AT LEAST this much.
    # Tighter than 5cm = depth-precision flicker at typical camera
    # distances (verified empirically).
    "z_fight_safety": 0.05,
}


# ---- JOINT derived helpers (single source of truth — DO NOT inline
#      these values; always call the helper so a future tweak of JOINT
#      propagates everywhere) ----

def corridor_outer_half_x():
    """X half-extent INCLUDING the corridor's side walls. Used as the
    OUTER face position of the corridor wall."""
    return JOINT["corridor_half_w"] + JOINT["corridor_wall_thickness"]


def corridor_opening_half_x():
    """X half-extent of an opening (back-wall tunnel, apse doorway)
    that must EXACTLY meet the corridor outer face. Back-wall flank
    inner X = corridor wall outer X = no air gap between them.
    Z-fight isn't a concern because the back-wall flank and the
    corridor wall sit at DIFFERENT Z (back wall at z=-3.4..-4.0,
    corridor wall starts at z=-3.15) — they share an X plane but not
    a 3D face."""
    return corridor_outer_half_x()


def corridor_ceiling_top_chapel_local_z(first_tread_z):
    """Chapel-local Z of the descent ceiling's TOP face at the corridor's
    chapel-side (high) end. first_tread_z is the chapel-local Z of the
    first tread top in the corridor (e.g. the bottom-of-upper-flight)."""
    return (first_tread_z
            + JOINT["corridor_ceiling_clearance"]
            + JOINT["corridor_ceiling_thickness"])


def apse_center_y():
    """Chapel-local Y of the apse cylinder center axis. Pushed 5cm
    BEHIND the back-wall outer face so the apse half-cut face is
    buried in back-wall material — not coplanar with it."""
    return BODY_LENGTH * 0.5 + JOINT["apse_back_wall_inset"]


def apse_far_y():
    """Chapel-local Y of the apse's far apex (the deepest point
    behind the back wall)."""
    return apse_center_y() + JOINT["apse_radius"]


def descent_wall_first_appearance_y():
    """Chapel-local Y where the descent corridor walls FIRST appear
    (chapel-side end). Starts at the corridor mouth (bottom of the
    upper flight = UPPER_FLIGHT_TOP_Y + step_count*tread). The
    chapel back-wall HEADER + WEDGE are sized assuming the corridor
    geometry starts here; moving this elsewhere creates a vertical
    gap between the wedge bottom and the corridor ceiling top.

    Volumetric overlap with crypt_wall_side at X=±2.4..±3.0 is a
    buried-face z-fight (invisible to the player from any angle
    since both primitives' outer faces are inside the other's solid)
    and acceptable per the existing audit baseline."""
    return UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * (
        UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD)


def apse_doorway_top_chapel_local_z(first_tread_z):
    """Chapel-local Z of the apse doorway top. Has TWO constraints:
      (a) must clear the corridor ceiling top by Z_FIGHT_SAFETY so the
          ceiling slab doesn't intersect apse material above the doorway
      (b) must be tall enough for the player to walk through (door height)
    Picks the higher of the two — currently door_height typically wins
    because the corridor enters the chapel just below floor level."""
    ceiling_clear = (corridor_ceiling_top_chapel_local_z(first_tread_z)
                     + JOINT["z_fight_safety"])
    door_clear = PLINTH_HEIGHT + DOOR_HEIGHT  # = chapel front-door header height
    return max(ceiling_clear, door_clear)


def continuous_descent_first_tread_z():
    """Chapel-local Z of the FIRST tread top in the continuous descent
    (= the step immediately after the upper-flight bottom). Shared by
    back-wall + apse + corridor so they all compute heights from the
    same reference."""
    upper_flight_drop = UPPER_FLIGHT_STEP_COUNT * STAIR_RISE
    descent_top_z = PLINTH_HEIGHT - upper_flight_drop  # = bottom of upper flight
    return descent_top_z - STAIR_RISE  # one rise below the upper-flight bottom


# ---------------------------------------------------------------------------
# Scene reset
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Per-region collection routing
# ---------------------------------------------------------------------------
# Every object created by add_box / add_merged_boxes /
# add_mesh_from_pydata is auto-linked into the CURRENT collection.
# The current collection is set by the `current_collection()` context
# manager, which main() wraps each build_*() group in. Helpers fall back
# to the scene root collection if no context is active (legacy path /
# tests).
#
# gen_crypt_export.py iterates these collection names and exports each
# to its own .glb file (chapel_exterior.glb, chapel_interior.glb).
# Keeping the JOINT dictionary's cross-zone constants in ONE generator
# + slicing at export time prevents seam drift between chapel and
# descent geometry. See [[feedback_dual_source_of_truth_is_the_bug]].

# Order matches the conceptual zones the .glbs end up representing.
COLLECTION_NAMES = ("chapel_exterior", "chapel_interior")

_current_collection_stack = []


class current_collection:
    """Context manager: while active, every new object created by the
    geometry helpers below is linked into `name` instead of the scene
    root. Stacks correctly if used nested."""

    def __init__(self, name):
        if name not in COLLECTION_NAMES:
            raise ValueError(f"unknown collection '{name}' (known: {COLLECTION_NAMES})")
        self.name = name

    def __enter__(self):
        _current_collection_stack.append(self.name)
        return self

    def __exit__(self, *_exc):
        _current_collection_stack.pop()


def _link_to_current(obj):
    """Link `obj` into the active collection set by current_collection(),
    or fall back to scene root if no context is in flight. Helpers call
    this instead of `bpy.context.scene.collection.objects.link(obj)`."""
    if _current_collection_stack:
        target = bpy.data.collections[_current_collection_stack[-1]]
    else:
        target = bpy.context.scene.collection
    # primitive_*_add already linked the object into the scene root, so
    # unlink there first to avoid duplicate-link errors.
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    if obj.name not in target.objects:
        target.objects.link(obj)


def reset_scene():
    """Delete everything in the default scene so we start clean, then
    pre-create the per-region collections the exporter slices on."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    for cname in COLLECTION_NAMES:
        new_coll = bpy.data.collections.new(cname)
        bpy.context.scene.collection.children.link(new_coll)


# ---------------------------------------------------------------------------
# Selection helpers — critical for ops to target the right object
# ---------------------------------------------------------------------------

def select_only(obj):
    """Deselect everything, then select+activate `obj`. Required before
    operators like transform_apply or modifier_apply."""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


# ---------------------------------------------------------------------------
# Material helpers
# ---------------------------------------------------------------------------

def make_material(name, rgb):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.85
    return mat


# Per-element material colors grounded in Vitaleta's real materials.
# Phase 1: linear-RGB base color factors only (no textures yet). The
# engine's StaticMeshAssets loader reads each primitive's
# baseColorFactor into StaticMeshPrimitive::base_color, which the
# scene shader uses via setSceneTint(luminance).
MAT_PIENZA = None       # Pietra di Pienza — warm beige sandstone, body + sides
MAT_TRAVERTINE = None   # Rapolano travertine — pale cream-white, façade
MAT_PIENZA_DARK = None  # Plinth + cornice — same family, slightly darker/weathered
MAT_TERRACOTTA = None   # Coppi roof tiles — orange-brown
MAT_DOOR_WOOD = None    # Stained oak/chestnut door
MAT_IRON = None         # Wrought iron cross, door studs (when added)
MAT_INTERIOR_STONE = None  # Cooler interior limewash — interior walls + ceiling
MAT_DESCENT_STONE = None   # Carved stone of the descent corridor — darker, older feel
def init_materials():
    global MAT_PIENZA, MAT_TRAVERTINE, MAT_PIENZA_DARK, MAT_TERRACOTTA
    global MAT_DOOR_WOOD, MAT_IRON, MAT_INTERIOR_STONE
    global MAT_DESCENT_STONE
    MAT_PIENZA = make_material("pienza_sandstone", (0.72, 0.65, 0.55))
    MAT_TRAVERTINE = make_material("rapolano_travertine", (0.86, 0.82, 0.74))
    MAT_PIENZA_DARK = make_material("pienza_weathered", (0.55, 0.49, 0.41))
    MAT_TERRACOTTA = make_material("roof_terracotta", (0.55, 0.28, 0.18))
    MAT_DOOR_WOOD = make_material("door_oak_stained", (0.18, 0.12, 0.08))
    MAT_IRON = make_material("iron_dark", (0.10, 0.09, 0.08))
    MAT_INTERIOR_STONE = make_material("interior_limewash", (0.46, 0.43, 0.38))
    # Descent corridor: darker carved stone. The lore-spec calls for
    # a gradual transition from Tuscan-stone (upper flights) to older
    # chthonic masonry (deeper corridor) — for v1 we use one darker
    # tone for everything below the upper flights; per-segment
    # gradient can be added later.
    MAT_DESCENT_STONE = make_material("descent_stone", (0.28, 0.25, 0.22))


# ---------------------------------------------------------------------------
# Geometry builders — low-level
# ---------------------------------------------------------------------------

def add_box(name, center, size, material=None):
    """Create an axis-aligned box of dimensions `size` (width-X, depth-Y,
    height-Z), centered at `center` (x, y, z). Returns the new object.
    Links into the current_collection() if one is active."""
    sx, sy, sz = size
    cx, cy, cz = center

    # primitive_cube_add creates a 2m cube (-1..+1 on each axis).
    # We pass size=1 then resize via scale, applying the scale so the
    # mesh's coordinates reflect the real dimensions.
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(cx, cy, cz))
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (sx, sy, sz)
    select_only(obj)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if material is not None:
        obj.data.materials.append(material)
    _link_to_current(obj)
    return obj


def add_merged_boxes(name, box_specs, material=None, usage=None):
    """Build ONE mesh containing the geometry of every box in box_specs
    (a list of (center, size) tuples). Used to collapse N stair-step
    primitives into a single render mesh (one VAO + one Jolt shape
    instead of N). Direct mesh construction via from_pydata is ~100x
    faster than 400 primitive_cube_add + object.join calls.

    `usage` is the StaticMeshUsage value for the C++ loader:
      None / "both"  = drawn AND collidable (default)
      "visual"       = drawn only (skip physics shape build)
      "collision"    = physics only (skip GL upload/draw)
    Stored as a Blender custom property; gen_crypt_export.py exports
    it as glTF node `extras` via export_extras=True.
    """
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
    """Generic single-mesh builder. Use for collision proxies (e.g. a
    single inclined ramp slab) where add_box geometry isn't a good fit.
    `usage` semantics match add_merged_boxes."""
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
    """Apply boolean DIFFERENCE: target minus cutter. Optionally remove cutter."""
    select_only(target)
    mod = target.modifiers.new(name="cut", type="BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    if delete_cutter:
        bpy.data.objects.remove(cutter, do_unlink=True)


# ---------------------------------------------------------------------------
# Geometry builders — high-level parts
# ---------------------------------------------------------------------------

# Authored interior floor — closes the chapel interior so terrain
# never participates as the walkable surface. Sits at plinth top
# (chapel-local Y = PLINTH_HEIGHT = 0.30) and spans the chapel
# interior X/Y, omitting the stair shaft. See pillar 8.
INTERIOR_FLOOR_THICKNESS = 0.10    # Z thickness of the floor slab


def build_interior_floor():
    """Authored interior floor slabs sitting on top of the plinth.
    Closes the chapel interior so terrain never acts as the walkable
    surface (pillar 8). Authored as 5 slabs around the stair shaft,
    mirroring the plinth pattern. The shaft remains open — player
    falls through into the descent.

    Slab top Y = PLINTH_HEIGHT + INTERIOR_FLOOR_THICKNESS (chapel-local).
    Player walks on the slab top, which is INTERIOR_FLOOR_THICKNESS
    above the plinth top exterior ground. That small step is hidden
    by the door threshold (which already ramps from terrain to plinth
    top).
    """
    # FromSoft intrusion rule (pillar 8): every floor edge that meets a
    # wall is INSET by z_fight_safety so the wall's interior face sits
    # outside the floor's outer edge. The wall buries the floor's
    # perimeter; no coplanar X/Z faces, no z-fight. Edges that meet
    # the shaft opening or the back-wall corridor opening are NOT
    # inset — those are interior boundaries, not wall meetings.
    zfs = JOINT["z_fight_safety"]
    half_w = BODY_WIDTH * 0.5 - zfs       # inset from interior wall plane
    half_l = BODY_LENGTH * 0.5 - zfs      # inset from front + back wall interior planes

    hole_half_w = SINGLE_FLIGHT_HALF_WIDTH
    hole_half_l = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD * 0.5
    hole_center_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * hole_half_l
    hole_y_near = hole_center_y - hole_half_l
    hole_y_far = hole_center_y + hole_half_l

    # Interior floor TOP sits 1mm ABOVE plinth top so the floor's
    # interior-stone material wins the depth test inside the chapel
    # (plinth top is buried below). 1mm is imperceptible to the
    # player; no step problem at the doorway. Body extends DOWN
    # through the plinth (buried; only the top face is visible).
    floor_top_z = PLINTH_HEIGHT + 0.001
    floor_bot_z = PLINTH_HEIGHT - INTERIOR_FLOOR_THICKNESS
    floor_cz = (floor_bot_z + floor_top_z) * 0.5
    floor_h = floor_top_z - floor_bot_z

    # Floor's shaft-facing edge inset by zfs from shaft rim so it
    # doesn't share the Y face with the plinth's shaft-edge face.
    # The plinth defines the visible shaft rim alone; floor edge
    # is buried zfs INSIDE the plinth solid.
    floor_hole_y_near = hole_y_near - JOINT["z_fight_safety"]
    floor_hole_y_far  = hole_y_far  + JOINT["z_fight_safety"]
    front_len = floor_hole_y_near - (-half_l)
    if front_len > 0.01:
        add_box(
            name="crypt_floor_front",
            center=(0.0, (-half_l + floor_hole_y_near) * 0.5, floor_cz),
            size=(2.0 * half_w, front_len, floor_h),
            material=MAT_INTERIOR_STONE,
        )

    back_len = half_l - floor_hole_y_far
    back_cy = (floor_hole_y_far + half_l) * 0.5
    # Floor's shaft-facing inner edge is buried INSIDE the upper-flight
    # wall by zfs (intrusion rule), so the upper-flight wall outer X
    # face buries the floor's shaft-facing edge. Without this, both
    # share X = SINGLE_FLIGHT_HALF_WIDTH plane.
    floor_back_opening_half_w = corridor_opening_half_x() + JOINT["z_fight_safety"]
    back_flank_w = half_w - floor_back_opening_half_w
    if back_len > 0.01 and back_flank_w > 0.01:
        for sign in (-1, 1):
            add_box(
                name=f"crypt_floor_back_{'r' if sign > 0 else 'l'}",
                center=(sign * (floor_back_opening_half_w + back_flank_w * 0.5),
                        back_cy, floor_cz),
                size=(back_flank_w, back_len, floor_h),
                material=MAT_INTERIOR_STONE,
            )

    floor_hole_half_w = hole_half_w + JOINT["z_fight_safety"]
    side_w = half_w - floor_hole_half_w
    # Side strips' Y range matches the shaft Y range INSET by zfs
    # (consistent with floor_hole_y_near/far above) — so the side
    # strips' +Y/-Y faces are also buried zfs inside the plinth, not
    # coplanar with plinth shaft-edge faces.
    floor_side_hole_y_near = floor_hole_y_near
    floor_side_hole_y_far  = floor_hole_y_far
    floor_side_center_y = (floor_side_hole_y_near + floor_side_hole_y_far) * 0.5
    floor_side_len      = floor_side_hole_y_far - floor_side_hole_y_near
    if side_w > 0.01 and floor_side_len > 0.01:
        for sign in (-1, 1):
            add_box(
                name=f"crypt_floor_{'r' if sign > 0 else 'l'}",
                center=(sign * (floor_hole_half_w + side_w * 0.5),
                        floor_side_center_y, floor_cz),
                size=(side_w, floor_side_len, floor_h),
                material=MAT_INTERIOR_STONE,
            )


def build_plinth():
    """Plinth (zoccolo) authored as 4 slabs around the stair hole rather
    than one box with a boolean cut. A boolean DIFFERENCE on a single
    primitive plinth produces sub-pixel slivers along the cut boundary
    after triangulation, visible in-game as a crooked rectangle outline
    on the otherwise-clean axis-aligned hole. Per-slab authoring makes
    the hole's silhouette exact by construction (same pattern as
    build_body — see its comment for the underlying issue)."""
    half_w = BODY_WIDTH * 0.5 + PLINTH_PROJECTION
    half_l = BODY_LENGTH * 0.5 + PLINTH_PROJECTION

    hole_half_w = SINGLE_FLIGHT_HALF_WIDTH
    hole_half_l = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD * 0.5
    hole_center_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * hole_half_l

    hole_y_near = hole_center_y - hole_half_l
    hole_y_far = hole_center_y + hole_half_l

    # Front strip (door side): full plinth width, from front edge of
    # plinth to near edge of the hole.
    front_len = hole_y_near - (-half_l)
    add_box(
        name="crypt_plinth_front",
        center=(0.0, (-half_l + hole_y_near) * 0.5, PLINTH_HEIGHT * 0.5),
        size=(2.0 * half_w, front_len, PLINTH_HEIGHT),
        material=MAT_PIENZA_DARK,
    )

    # Back strip (apse side): TWO flanking slabs leaving the corridor
    # opening X span clear. Opening X = corridor_opening_half_x (same
    # as back-wall tunnel + apse doorway — single consistent opening
    # through the whole back of the chapel).
    back_len = half_l - hole_y_far
    back_strip_cy = (hole_y_far + half_l) * 0.5
    plinth_opening_half_w = corridor_opening_half_x()
    back_flank_w = half_w - plinth_opening_half_w
    if back_len > 0.01 and back_flank_w > 0.01:
        for sign in (-1, 1):
            add_box(
                name=f"crypt_plinth_back_{'r' if sign > 0 else 'l'}",
                center=(sign * (plinth_opening_half_w + back_flank_w * 0.5),
                        back_strip_cy, PLINTH_HEIGHT * 0.5),
                size=(back_flank_w, back_len, PLINTH_HEIGHT),
                material=MAT_PIENZA_DARK,
            )

    # Left + right side strips along the hole's Y range. Span:
    # interior wall face (X = ±SINGLE_FLIGHT_HALF_WIDTH = ±chapel
    # interior face) out to plinth outer edge, with FULL plinth height
    # (Z=0..PLINTH_HEIGHT). These sit ENTIRELY UNDER the chapel side
    # walls; their inner edge stops at the wall's interior face (= the
    # stair shaft's outer X), so no overhang into the stair. Without
    # them, the 0.3m vertical gap between chapel-wall-bottom (Z=0.30)
    # and stair-shaft-floor leaves the side of the chapel exposed —
    # player sees terrain bleed through under the wall edges.
    side_strip_inner_x = SINGLE_FLIGHT_HALF_WIDTH  # = chapel interior face
    side_strip_outer_x = BODY_WIDTH * 0.5 + PLINTH_PROJECTION
    side_strip_w = side_strip_outer_x - side_strip_inner_x
    for sign in (-1, 1):
        add_box(
            name=f"crypt_plinth_{'r' if sign > 0 else 'l'}",
            center=(sign * (side_strip_inner_x + side_strip_w * 0.5),
                    hole_center_y, PLINTH_HEIGHT * 0.5),
            size=(side_strip_w, 2.0 * hole_half_l, PLINTH_HEIGHT),
            material=MAT_PIENZA_DARK,
        )


def build_foundation_skirt():
    """Vertical stone foundation extending from the plinth bottom
    (chapel-local Z=0) DOWN to a deep Z so the chapel's ground geometry
    bridges any terrain height around it. Without a skirt, lowering the
    colle (or any change to terrain) leaves the chapel floating above
    the actual ground; with a skirt, terrain meets the skirt's vertical
    face at whatever local height it happens to be — chapel decouples
    from terrain entirely (see [[feedback_structures_own_terrain_seam]]).

    Mirror of `build_plinth` (4 slabs around the descent hole), just
    deeper. Same X/Y footprint so the two stack into one continuous
    vertical face from skirt-bottom through plinth-top.

    FromSoft intrusion rule (see [[feedback_authored_architecture_coplanar_zfight]]):
    the skirt OVERLAPS adjacent primitives by z_fight_safety on every
    shared face so no two surfaces share a plane. Top face buries UP
    into the plinth, side faces toward the descent shaft bury INWARD
    past the stair-shaft walls.
    """
    half_w = BODY_WIDTH * 0.5 + PLINTH_PROJECTION
    half_l = BODY_LENGTH * 0.5 + PLINTH_PROJECTION

    # Skirt depth: just enough to bridge between chapel plinth bottom
    # and the colle terrain around the chapel. The colle peaks at
    # plateau_height m above wake_zone_y, locally varying by a meter
    # or so from noise. 5m gives generous margin (chapel can sit up
    # to ~5m above local terrain without exposing the skirt's bottom
    # face). Larger values produce a "pillar dangling underground"
    # silhouette visible from Limbo before the ceiling mesh is in
    # place — there's no benefit to going deeper than needed.
    SKIRT_DEPTH = 5.0
    zfs = JOINT["z_fight_safety"]

    hole_half_w = SINGLE_FLIGHT_HALF_WIDTH
    hole_half_l = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD * 0.5
    hole_center_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * hole_half_l
    hole_y_near = hole_center_y - hole_half_l
    hole_y_far = hole_center_y + hole_half_l

    # Skirt covers ONLY the front half of the chapel (door side) where
    # there is no descent geometry below. The back half + stair-shaft
    # interior are already enclosed all the way down by descent_wall_l/r
    # (X=±2.40..3.00, Y full corridor span) and the stair_upper_wall
    # primitives (X=±2.40..2.70, Y above the corridor). Adding a skirt
    # that wraps the shaft creates coplanar faces with the existing
    # descent walls (z-fight). One front-half slab is enough; the back
    # is sealed by descent geometry.
    # Skirt top buries UP into the plinth by zfs so its top face never
    # coincides with door_threshold's bottom face at Z=0. The skirt's
    # footprint is entirely inside the plinth's XY footprint, so the
    # buried top face has no other primitive to conflict with.
    skirt_top_z = +zfs
    skirt_bot_z = -SKIRT_DEPTH
    skirt_center_z = (skirt_top_z + skirt_bot_z) * 0.5
    skirt_height = skirt_top_z - skirt_bot_z

    # Front slab: full footprint width, from front edge of skirt to
    # the descent shaft's near edge (hole_y_near). Side / front /
    # back faces are inset by zfs so they sit JUST INSIDE the plinth's
    # outer faces (no coplanar). Top is buried into the plinth (see
    # skirt_top_z above).
    front_x_outer = half_w - zfs
    # Front face buries inward past the door_threshold's inner face.
    # door_threshold sits OUT in front of the chapel (Y < -half_l) with
    # its inner face at exactly Y = -half_l. Just insetting past the
    # plinth (-half_l + zfs = -4.0) would land on door_threshold's
    # inner face — coplanar z-fight. Push past it (door_threshold's
    # inner face + zfs).
    front_y_outer = -half_l + 2.0 * zfs
    front_y_inner = hole_y_near + zfs  # bury inward past plinth's inner stair-side edge (and clear of stair_upper_wall front face at Y=0)
    front_len = front_y_inner - front_y_outer
    add_box(
        name="crypt_foundation_skirt_front",
        center=(0.0, (front_y_outer + front_y_inner) * 0.5, skirt_center_z),
        size=(2.0 * front_x_outer, front_len, skirt_height),
        material=MAT_PIENZA_DARK,
    )


def build_door_threshold():
    """Stone threshold ramp sloping up from the surrounding terrain
    to the plinth top at the doorway. Without this, the plinth's
    0.30m vertical lip on its exterior face blocks the player from
    walking through the door — CharacterVirtual's default step-up
    height is 0.4m but the lip occurs at ground level where the
    capsule's lower hemisphere fully contacts the wall.

    Real-physics doctrine: doorways have thresholds. A real chapel
    has a stone stoop / sloped sill / step you walk over. Adding a
    short ramp leading up to the door makes traversal natural
    (gravity + step-up handle it) without needing physics tuning.

    Geometry: a wedge whose top edge meets the plinth top at the
    door's outer face (Y = -BODY_LENGTH/2 - PLINTH_PROJECTION) and
    slopes down to ground level (Z=0) over THRESHOLD_RUN meters
    outward. Width matches DOOR_WIDTH so it doesn't visually clash
    with the rest of the plinth face.
    """
    threshold_run = 0.50  # how far the ramp extends in front of the door
    door_outer_y = -(BODY_LENGTH * 0.5 + PLINTH_PROJECTION)
    half_w = DOOR_WIDTH * 0.5
    # 8 verts of a wedge (top sloped, bottom flat at ground).
    # Up-slope edge (against plinth front face) at Z=PLINTH_HEIGHT,
    # down-slope edge (forward of door) at Z=0.
    mesh = bpy.data.meshes.new("crypt_door_threshold_mesh")
    obj = bpy.data.objects.new("crypt_door_threshold", mesh)
    _link_to_current(obj)
    bm = bmesh.new()
    # FromSoft intrusion rule: threshold's plinth-side face sits
    # z_fight_safety INSIDE the plinth solid so the plinth front face
    # buries it (no coplanar Y face at door_outer_y).
    y_in = door_outer_y + JOINT["z_fight_safety"]  # buried inside plinth
    y_out = door_outer_y - threshold_run            # forward of door
    v_top_in_l  = bm.verts.new((-half_w, y_in,  PLINTH_HEIGHT))
    v_top_in_r  = bm.verts.new(( half_w, y_in,  PLINTH_HEIGHT))
    v_top_out_l = bm.verts.new((-half_w, y_out, 0.0))
    v_top_out_r = bm.verts.new(( half_w, y_out, 0.0))
    v_bot_in_l  = bm.verts.new((-half_w, y_in,  0.0))
    v_bot_in_r  = bm.verts.new(( half_w, y_in,  0.0))
    v_bot_out_l = bm.verts.new((-half_w, y_out, 0.0))
    v_bot_out_r = bm.verts.new(( half_w, y_out, 0.0))
    bm.faces.new([v_top_in_l, v_top_in_r, v_top_out_r, v_top_out_l])     # top (sloped)
    bm.faces.new([v_bot_out_l, v_bot_out_r, v_bot_in_r, v_bot_in_l])     # bottom (flat)
    bm.faces.new([v_top_in_l, v_bot_in_l, v_bot_out_l, v_top_out_l])     # -X side
    bm.faces.new([v_top_out_r, v_bot_out_r, v_bot_in_r, v_top_in_r])     # +X side
    bm.faces.new([v_top_in_r, v_top_in_l, v_bot_in_l, v_bot_in_r])       # +Y end (against plinth)
    bm.faces.new([v_top_out_l, v_top_out_r, v_bot_out_r, v_bot_out_l])   # -Y end (forward)
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    if MAT_PIENZA_DARK is not None:
        obj.data.materials.append(MAT_PIENZA_DARK)


def build_body():
    """Main rectangular nave authored as discrete wall slabs — back,
    left, right, ceiling, floor, plus two front-wall halves flanking
    the door gap.

    Booleans on a single hollowed box produce degenerate slivers at
    the corners that the shadow pass can see through (visible as
    bright vertical seams inside). Authoring per-wall slabs eliminates
    those by construction: each wall is a clean box with no boolean
    cuts.
    """
    half_w = BODY_WIDTH * 0.5
    half_l = BODY_LENGTH * 0.5
    wt = WALL_THICKNESS
    door_hw = DOOR_WIDTH * 0.5
    floor_z = PLINTH_HEIGHT
    ceiling_z = BODY_HEIGHT_TO_CORNICE - wt
    interior_h = ceiling_z - floor_z
    wall_center_z = (floor_z + ceiling_z) * 0.5

    # Wall tops sit at the cornice's UNDERSIDE (cornice base z), not
    # at BODY_HEIGHT_TO_CORNICE. The cornice ring then sits ON TOP of
    # the walls with no volumetric overlap — coplanar exterior faces
    # would otherwise z-fight, visible as a flickering rectangle at
    # the top of each wall's outer surface.
    wall_top_z = BODY_HEIGHT_TO_CORNICE - CORNICE_HEIGHT
    side_wall_h = wall_top_z - floor_z
    side_wall_center_z = (floor_z + wall_top_z) * 0.5
    # Side walls END at the corridor mouth (chapel-local Y =
    # descent_wall_first_appearance_y, currently +3.15) instead of the
    # chapel back wall (+4.00). The descent walls take over from there,
    # extending past the back wall into the descent. Without this trim
    # the chapel side walls and descent walls volumetrically overlap in
    # chapel-local Y[+3.15, +4.00] at X=±2.4/±3.0 — visible flicker on
    # both faces from inside the chapel + corridor mouth.
    side_wall_y_back = descent_wall_first_appearance_y()  # +3.15
    side_wall_y_front = -half_l                            # -4.00
    side_wall_len_y = side_wall_y_back - side_wall_y_front
    side_wall_center_y = (side_wall_y_back + side_wall_y_front) * 0.5
    for sign in (-1, 1):
        add_box(
            name=f"crypt_wall_side_{'right' if sign > 0 else 'left'}",
            center=(sign * (half_w - wt * 0.5), side_wall_center_y, side_wall_center_z),
            size=(wt, side_wall_len_y, side_wall_h),
            material=MAT_PIENZA,
        )

    # Interior width = body width minus the two side-wall thicknesses.
    interior_w = BODY_WIDTH - 2.0 * wt

    # Back wall: 3 slabs (left + right of the stair-tunnel opening,
    # upper above it) + header. All four slabs share the SAME Y
    # (depth) range so the chapel back wall "molds" around the
    # descent corridor — no air gap between the back wall and the
    # descent corridor near-end.
    #
    # Y range: from back-wall back face (y = +half_l = +4.00) FORWARD
    # to JUST BEFORE the descent corridor's chapel-side end (y =
    # upper flight bottom = +3.15). The back-wall front face is set
    # at +3.15 + z_fight_safety so it is NOT coplanar with the
    # descent corridor ceiling chapel-side face — the back wall
    # surface is INSIDE the corridor primitive's interior (buried).
    # Eliminates the z-fight while keeping the surfaces flush within
    # depth-precision tolerance.
    z_safe = JOINT["z_fight_safety"]
    back_wall_far_y = half_l                                       # +4.00
    back_wall_near_y = (UPPER_FLIGHT_TOP_Y +
                       DESCENT_FORWARD_SIGN *
                       UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD
                       + z_safe)                                    # +3.20
    back_wall_depth = abs(back_wall_far_y - back_wall_near_y)
    back_wall_cy = (back_wall_far_y + back_wall_near_y) * 0.5
    # Back-wall opening X half-extent. Set to chapel INTERIOR half so
    # the wedge + back-upper slab fit inside the chapel back wall area
    # without overlapping the chapel side walls (which span X[±2.4,±3.0]).
    back_tunnel_half_w = BODY_WIDTH * 0.5 - WALL_THICKNESS
    back_tunnel_top_z = apse_doorway_top_chapel_local_z(
        continuous_descent_first_tread_z())
    back_side_h = back_tunnel_top_z - floor_z
    back_side_center_z = (floor_z + back_tunnel_top_z) * 0.5
    back_outer_x = interior_w * 0.5
    back_segment_w = back_outer_x - back_tunnel_half_w
    if back_segment_w > 0.01:
        add_box(
            name="crypt_wall_back_left",
            center=(-(back_outer_x + back_tunnel_half_w) * 0.5,
                    back_wall_cy, back_side_center_z),
            size=(back_segment_w, back_wall_depth, back_side_h),
            material=MAT_PIENZA,
        )
        add_box(
            name="crypt_wall_back_right",
            center=((back_outer_x + back_tunnel_half_w) * 0.5,
                    back_wall_cy, back_side_center_z),
            size=(back_segment_w, back_wall_depth, back_side_h),
            material=MAT_PIENZA,
        )
    # Back upper slab — spans the FULL chapel exterior width (not just
    # interior). With chapel side walls trimmed to end at the corridor
    # mouth, the X=[±half_w_interior, ±half_w_exterior] columns in the
    # back wall area would be empty without this. Extending back_upper
    # to full chapel width closes the back corners.
    back_upper_h = wall_top_z - back_tunnel_top_z
    if back_upper_h > 0.01:
        add_box(
            name="crypt_wall_back_upper",
            center=(0.0, back_wall_cy,
                    back_tunnel_top_z + back_upper_h * 0.5),
            size=(BODY_WIDTH, back_wall_depth, back_upper_h),
            material=MAT_PIENZA,
        )

    # Back header wedge — fills the triangular gap between the descent
    # corridor CEILING top (SLOPED) and the chapel back-wall opening
    # top (HORIZONTAL). The descent ceiling descends ~0.45m per meter
    # forward; over the back-wall depth, the ceiling top drops below
    # the opening top significantly. A flat header slab can't match
    # this slope. We build a custom wedge whose TOP is horizontal at
    # back_tunnel_top_z and whose BOTTOM follows the descent ceiling
    # top slope exactly.
    #
    # Geometry: 8-vertex box where the TOP-Z is constant across all
    # four top corners, but the BOTTOM-Z varies between the high-Y
    # (chapel-side) and low-Y (back-wall back face) ends matching
    # the descent ceiling slope.
    # IMPORTANT: descent corridor first tread Y is at the BOTTOM of
    # the upper flight (+3.15), NOT at chapel center (+0). The
    # continuous descent starts AFTER the upper flight ends.
    upper_flight_run = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD
    descent_top_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * upper_flight_run
    first_tread_z_local = continuous_descent_first_tread_z()
    first_tread_y_local = descent_top_y + DESCENT_FORWARD_SIGN * 0.5 * STAIR_TREAD
    last_tread_z_local  = first_tread_z_local - (
        CONTINUOUS_DESCENT_STEP_COUNT - 1) * STAIR_RISE
    last_tread_y_local  = first_tread_y_local + DESCENT_FORWARD_SIGN * (
        CONTINUOUS_DESCENT_STEP_COUNT - 1) * STAIR_TREAD
    slope_drop_per_y = ((last_tread_z_local - first_tread_z_local)
                        / (last_tread_y_local - first_tread_y_local))
    # Ceiling top Z at the chapel-side (high) end of the wedge.
    ceil_top_at_near_y = (first_tread_z_local +
                          JOINT["corridor_ceiling_clearance"] +
                          JOINT["corridor_ceiling_thickness"])
    # Ceiling top Z at the back-wall back face (low) end.
    delta_y = back_wall_far_y - first_tread_y_local
    ceil_top_at_far_y = ceil_top_at_near_y + delta_y * slope_drop_per_y
    # Build the wedge mesh by hand. 8 corners: high-Y / low-Y × ±X × top/bottom.
    # Wedge X extends to chapel EXTERIOR width (BODY_WIDTH/2) to fill
    # the back corners (chapel side walls now end at the corridor mouth
    # Y=+3.15, leaving those corners empty). Inset by z_fight_safety
    # on outer-X so the wedge's outer face is buried inside the
    # descent wall solid (descent walls' outer face at X=±3.0).
    hx = BODY_WIDTH * 0.5 - JOINT["z_fight_safety"]
    y_near = back_wall_near_y
    y_far = back_wall_far_y
    z_top = back_tunnel_top_z
    z_bot_near = ceil_top_at_near_y
    z_bot_far = ceil_top_at_far_y
    wedge_mesh = bpy.data.meshes.new("crypt_wall_back_header_mesh")
    wedge_obj = bpy.data.objects.new("crypt_wall_back_header", wedge_mesh)
    _link_to_current(wedge_obj)
    bm = bmesh.new()
    # Top-near edge (high-Y end, horizontal top)
    v_top_near_l = bm.verts.new((-hx, y_near, z_top))
    v_top_near_r = bm.verts.new((+hx, y_near, z_top))
    # Top-far edge (low-Y end, horizontal top)
    v_top_far_l  = bm.verts.new((-hx, y_far,  z_top))
    v_top_far_r  = bm.verts.new((+hx, y_far,  z_top))
    # Bottom-near edge (high-Y end, at ceiling-top-near Z)
    v_bot_near_l = bm.verts.new((-hx, y_near, z_bot_near))
    v_bot_near_r = bm.verts.new((+hx, y_near, z_bot_near))
    # Bottom-far edge (low-Y end, at ceiling-top-far Z — LOWER)
    v_bot_far_l  = bm.verts.new((-hx, y_far,  z_bot_far))
    v_bot_far_r  = bm.verts.new((+hx, y_far,  z_bot_far))
    # 6 faces with consistent outward-facing winding.
    bm.faces.new([v_top_near_l, v_top_near_r, v_top_far_r,  v_top_far_l])   # top (horizontal)
    bm.faces.new([v_bot_far_l,  v_bot_far_r,  v_bot_near_r, v_bot_near_l])  # bottom (sloped)
    bm.faces.new([v_top_near_l, v_top_far_l,  v_bot_far_l,  v_bot_near_l])  # -X side
    bm.faces.new([v_top_far_r,  v_top_near_r, v_bot_near_r, v_bot_far_r])   # +X side
    bm.faces.new([v_top_near_r, v_top_near_l, v_bot_near_l, v_bot_near_r])  # high-Y (chapel-side)
    bm.faces.new([v_top_far_l,  v_top_far_r,  v_bot_far_r,  v_bot_far_l])   # low-Y (back-wall back face)
    bm.normal_update()
    bm.to_mesh(wedge_mesh)
    bm.free()
    if MAT_PIENZA is not None:
        wedge_mesh.materials.append(MAT_PIENZA)

    # Front wall: three slabs (left + right of the door, upper above
    # it). The combined width is interior_w; the door gap is centered.
    front_y = -half_l + wt * 0.5
    door_top_z = PLINTH_HEIGHT + DOOR_HEIGHT
    side_h = door_top_z - PLINTH_HEIGHT
    side_center_z = (PLINTH_HEIGHT + door_top_z) * 0.5
    front_outer_x = interior_w * 0.5
    front_segment_w = front_outer_x - door_hw
    add_box(
        name="crypt_wall_front_left",
        center=(-(front_outer_x + door_hw) * 0.5, front_y, side_center_z),
        size=(front_segment_w, wt, side_h),
        material=MAT_PIENZA,
    )
    add_box(
        name="crypt_wall_front_right",
        center=((front_outer_x + door_hw) * 0.5, front_y, side_center_z),
        size=(front_segment_w, wt, side_h),
        material=MAT_PIENZA,
    )
    # Front upper slab — top at cornice underside (same as the side
    # walls and back wall).
    const_upper_height = wall_top_z - door_top_z
    if const_upper_height > 0.01:
        add_box(
            name="crypt_wall_front_upper",
            center=(0.0, front_y, door_top_z + const_upper_height * 0.5),
            size=(interior_w, wt, const_upper_height),
            material=MAT_PIENZA,
        )

    # Debug door seal: fill the door opening with a slab so the
    # interior is fully enclosed. Used to verify whether visible
    # interior light is coming through the door.
    if DEBUG_SEAL_DOOR:
        seal_h = door_top_z - PLINTH_HEIGHT
        seal_center_z = (PLINTH_HEIGHT + door_top_z) * 0.5
        add_box(
            name="crypt_wall_front_door_seal",
            center=(0.0, front_y, seal_center_z),
            size=(DOOR_WIDTH, wt, seal_h),
            material=MAT_PIENZA,
        )

    # Floor is the plinth's top surface — no separate slab. Previous
    # versions had a `crypt_floor` slab coplanar with the plinth at
    # Z=[0, PLINTH_HEIGHT] which z-fought visibly and made boolean
    # cuts (stair holes) leave overlapping residue. The plinth's top
    # at Z=PLINTH_HEIGHT serves as the chapel floor.

    # Ceiling slab — sits ON TOP of the wall tops. Spans only the
    # INTERIOR cavity (between front/back walls and side walls);
    # the wall slabs themselves cover everything else at this Z
    # range. Earlier this spanned the full body length, putting its
    # south face coplanar with the back wall's outer face — visible
    # as a flickering horizontal rectangle at the top of the back
    # wall's exterior.
    const_ceiling_thickness = wt
    add_box(
        name="crypt_ceiling",
        center=(0.0, 0.0, ceiling_z + const_ceiling_thickness * 0.5),
        size=(BODY_WIDTH - 2.0 * wt, BODY_LENGTH - 2.0 * wt, const_ceiling_thickness),
        material=MAT_INTERIOR_STONE,
    )

    return bpy.data.objects.get("crypt_wall_front_upper")


def build_facade_overlay():
    """Dressed Rapolano travertine cladding on the front face — three
    slabs flanking the door + spanning above it, matching the body's
    front-wall structure so the cladding has the door cut by
    construction (no boolean). Cladding spans the full body width
    here (it sits OUTSIDE the body, doesn't share corners with side
    walls)."""
    half_w = BODY_WIDTH * 0.5
    half_l = BODY_LENGTH * 0.5
    door_hw = DOOR_WIDTH * 0.5
    door_top_z = PLINTH_HEIGHT + DOOR_HEIGHT
    side_h = door_top_z - PLINTH_HEIGHT
    side_center_z = (PLINTH_HEIGHT + door_top_z) * 0.5
    cladding_thickness = 0.03
    # Push cladding 5mm INTO the front wall so its inner face is
    # buried in solid stone (not coplanar with the front wall's outer
    # face — different materials would z-fight there).
    cladding_inset = 0.005
    cladding_y = -half_l - (cladding_thickness * 0.5 - cladding_inset)
    cladding_top_z = BODY_HEIGHT_TO_CORNICE - CORNICE_HEIGHT  # below cornice
    # Cladding outer-X face inset by zfs to bury inside side wall solid.
    # Cladding Z range inset by zfs top + bottom to bury inside the
    # plinth (bottom) and cornice (top) so the cladding's horizontal
    # faces aren't coplanar with wall horizontal faces.
    zfs = JOINT["z_fight_safety"]
    cladding_x_outer = half_w - zfs
    cladding_z_bot = PLINTH_HEIGHT + zfs
    cladding_z_top_side = door_top_z - zfs  # for left/right segments below door
    cladding_z_top_upper = cladding_top_z - zfs
    cladding_z_bot_upper = door_top_z + zfs
    side_h_inset = cladding_z_top_side - cladding_z_bot
    side_center_z_inset = (cladding_z_bot + cladding_z_top_side) * 0.5
    upper_h_inset = cladding_z_top_upper - cladding_z_bot_upper
    upper_center_z_inset = (cladding_z_bot_upper + cladding_z_top_upper) * 0.5
    # Cladding door-side X edge inset by zfs to bury inside the wall
    # (avoids coplanar X face at X=±0.5 with wall_front_left/right).
    cladding_door_hw = door_hw + zfs
    segment_w = cladding_x_outer - cladding_door_hw
    add_box(
        name="crypt_facade_cladding_left",
        center=(-(cladding_x_outer + cladding_door_hw) * 0.5, cladding_y, side_center_z_inset),
        size=(segment_w, cladding_thickness, side_h_inset),
        material=MAT_TRAVERTINE,
    )
    add_box(
        name="crypt_facade_cladding_right",
        center=((cladding_x_outer + cladding_door_hw) * 0.5, cladding_y, side_center_z_inset),
        size=(segment_w, cladding_thickness, side_h_inset),
        material=MAT_TRAVERTINE,
    )
    add_box(
        name="crypt_facade_cladding_upper",
        center=(0.0, cladding_y, upper_center_z_inset),
        size=(2.0 * cladding_x_outer, cladding_thickness, upper_h_inset),
        material=MAT_TRAVERTINE,
    )


def build_cornice():
    """Plain cornice running around the body just below the pediment.

    Shifted DOWN by `cornice_overlap` so its underside is below the
    walls' tops by that amount — the walls' top faces are buried
    inside the cornice volume, not coplanar with the cornice's
    underside (which would z-fight as a darker horizontal band at
    the top of each wall)."""
    cornice_overlap = 0.005
    return add_box(
        name="crypt_cornice",
        center=(0.0, 0.0, BODY_HEIGHT_TO_CORNICE - CORNICE_HEIGHT * 0.5 - cornice_overlap),
        size=(
            BODY_WIDTH + 2.0 * CORNICE_PROJECTION,
            BODY_LENGTH + 2.0 * CORNICE_PROJECTION,
            CORNICE_HEIGHT,
        ),
        material=MAT_TRAVERTINE,
    )


def build_corner_pilasters():
    """Four flat lesene at the corners of the body. Vitaleta's are very
    subtle (2cm projection)."""
    pilasters = []
    half_w = BODY_WIDTH * 0.5
    half_l = BODY_LENGTH * 0.5
    # Pilaster Z range inset by zfs on top + bottom so the pilaster's
    # horizontal faces are buried inside the plinth (bottom) and
    # cornice (top) — not coplanar with the wall's top/bottom Y faces.
    pil_zfs = JOINT["z_fight_safety"]
    pil_bot_z = PLINTH_HEIGHT + pil_zfs
    pil_top_z = (BODY_HEIGHT_TO_CORNICE - CORNICE_HEIGHT) - pil_zfs
    pil_h = pil_top_z - pil_bot_z
    pil_center_z = (pil_bot_z + pil_top_z) * 0.5
    # One pilaster on each of the four corners, projecting from the
    # FRONT and BACK faces. (Pilasters on the long sides would be a
    # separate decision; Vitaleta has them mainly on the façade.)
    # Pilasters sit IN FRONT of either the cladding (front corners) or
    # the back wall (back corners). X inset (5mm) buries the outer-X
    # face inside the chapel side wall solid. Y inset must clear the
    # cladding's outer face on front corners (cladding is 3cm-thick
    # veneer at chapel front; back corners just sit against the wall).
    pilaster_inset_x = 0.005
    # FRONT pilaster back face must be INSIDE the cladding solid by
    # zfs. cladding outer face is at ±(half_l + cladding_thickness -
    # cladding_inset) = ±4.025; pilaster back at ±(4.025 - zfs).
    pilaster_back_y_front = BODY_LENGTH * 0.5 + 0.03 - 0.005 - JOINT["z_fight_safety"]
    # BACK pilaster back face inside the wall solid by zfs.
    pilaster_back_y_back  = BODY_LENGTH * 0.5 - JOINT["z_fight_safety"]
    for i, (sign_x, sign_y) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        # Pull pilaster's outer-X face 5mm inward so it's NOT coplanar
        # with side wall outer face at x=±half_w. The outer edge ends
        # up at x=±(half_w - inset), buried 5mm inside the side wall.
        center_x = sign_x * (half_w - PILASTER_WIDTH * 0.5 - pilaster_inset_x)
        pilaster_back = pilaster_back_y_front if sign_y < 0 else pilaster_back_y_back
        center_y = sign_y * (pilaster_back + PILASTER_PROJECTION * 0.5)
        p = add_box(
            name=f"crypt_pilaster_{i}",
            center=(center_x, center_y, pil_center_z),
            size=(PILASTER_WIDTH, PILASTER_PROJECTION, pil_h),
            material=MAT_TRAVERTINE,
        )
        pilasters.append(p)
    return pilasters


def build_pediment():
    """Triangular pediment on top of the front wall.

    Built as a triangular prism whose Y-axis depth matches the body's
    length. Sits ON TOP of the body at the cornice line.
    """
    mesh = bpy.data.meshes.new("crypt_pediment_mesh")
    obj = bpy.data.objects.new("crypt_pediment", mesh)
    _link_to_current(obj)

    half_w = BODY_WIDTH * 0.5
    z_base = BODY_HEIGHT_TO_CORNICE
    z_apex = z_base + PEDIMENT_HEIGHT
    front_y = -BODY_LENGTH * 0.5
    back_y = BODY_LENGTH * 0.5

    bm = bmesh.new()
    v1 = bm.verts.new((-half_w, front_y, z_base))
    v2 = bm.verts.new((+half_w, front_y, z_base))
    v3 = bm.verts.new((0.0, front_y, z_apex))
    v4 = bm.verts.new((-half_w, back_y, z_base))
    v5 = bm.verts.new((+half_w, back_y, z_base))
    v6 = bm.verts.new((0.0, back_y, z_apex))

    bm.faces.new([v1, v2, v3])            # front triangle
    bm.faces.new([v6, v5, v4])            # back triangle
    bm.faces.new([v1, v3, v6, v4])        # left slope
    bm.faces.new([v3, v2, v5, v6])        # right slope
    bm.faces.new([v1, v4, v5, v2])        # bottom (sits on body)

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    if MAT_PIENZA is not None:
        obj.data.materials.append(MAT_PIENZA)
    return obj


def build_roof():
    """Pitched roof: two angled planes meeting at the ridge.

    Sits ON TOP of the body + pediment. The ridge runs front-to-back
    (along Y axis). The roof slopes down toward +X and -X sides.

    Built as a separate object so it can carry the terracotta material.
    Modeled as two thin angled slabs joined along the ridge.
    """
    mesh = bpy.data.meshes.new("crypt_roof_mesh")
    obj = bpy.data.objects.new("crypt_roof", mesh)
    _link_to_current(obj)

    half_w = BODY_WIDTH * 0.5
    z_eave = BODY_HEIGHT_TO_CORNICE
    z_ridge = BODY_HEIGHT_TO_CORNICE + PEDIMENT_HEIGHT
    # Extend roof past the pediment front+back triangles by 5cm so
    # the roof front/back slope edges are NOT coplanar with the
    # pediment's front/back triangular faces. Coplanar caused
    # visible z-fight flicker on the triangular pediment slopes
    # as the camera moved.
    roof_y_overshoot = 0.05
    front_y = -BODY_LENGTH * 0.5 - roof_y_overshoot
    back_y = BODY_LENGTH * 0.5 + roof_y_overshoot

    # Roof extends laterally past the body by ROOF_OVERHANG on the long sides.
    overhang_x = half_w + ROOF_OVERHANG

    bm = bmesh.new()
    # Top surface vertices (eave + ridge, front and back).
    v_fl = bm.verts.new((-overhang_x, front_y, z_eave))   # front left eave
    v_fr = bm.verts.new((+overhang_x, front_y, z_eave))   # front right eave
    v_bl = bm.verts.new((-overhang_x, back_y, z_eave))    # back left eave
    v_br = bm.verts.new((+overhang_x, back_y, z_eave))    # back right eave
    v_fc = bm.verts.new((0.0, front_y, z_ridge))           # front ridge
    v_bc = bm.verts.new((0.0, back_y, z_ridge))            # back ridge

    # Two angled top faces (left slope, right slope), normals up.
    bm.faces.new([v_fl, v_fc, v_bc, v_bl])
    bm.faces.new([v_fc, v_fr, v_br, v_bc])

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    if MAT_TERRACOTTA is not None:
        obj.data.materials.append(MAT_TERRACOTTA)
    return obj


def hollow_body():
    """Subtract an inner box from the body so the interior is walkable.

    Leaves WALL_THICKNESS of stone on the four sides + the ceiling, with
    the interior floor sitting on top of the plinth (z=PLINTH_HEIGHT).
    The interior ceiling sits at z=BODY_HEIGHT_TO_CORNICE - WALL_THICKNESS.
    """
    body = bpy.data.objects.get("crypt_body")
    if body is None:
        print("[gen_crypt_foundation] WARN: crypt_body not found for hollow")
        return

    interior_width = BODY_WIDTH - 2.0 * WALL_THICKNESS
    interior_length = BODY_LENGTH - 2.0 * WALL_THICKNESS
    interior_floor_z = PLINTH_HEIGHT
    interior_ceiling_z = BODY_HEIGHT_TO_CORNICE - WALL_THICKNESS
    interior_height = interior_ceiling_z - interior_floor_z
    interior_center_z = (interior_floor_z + interior_ceiling_z) * 0.5

    cutter = add_box(
        name="_interior_cutter",
        center=(0.0, 0.0, interior_center_z),
        size=(interior_width, interior_length, interior_height),
    )
    boolean_difference(body, cutter)


def cut_door():
    """Cut the architraved door through the FRONT wall — punches from
    outside all the way into the hollow interior."""
    body = bpy.data.objects.get("crypt_body")
    if body is None:
        print("[gen_crypt_foundation] WARN: crypt_body not found for door cut")
        return
    # Cutter overlaps the entire front-face slab. WALL_THICKNESS + slop
    # ensures it punches through to the interior cavity even with
    # rounding from prior boolean ops.
    cutter_depth = WALL_THICKNESS + 0.40
    cutter = add_box(
        name="_door_cutter",
        center=(
            0.0,
            -BODY_LENGTH * 0.5 + cutter_depth * 0.5 - 0.10,
            PLINTH_HEIGHT + DOOR_HEIGHT * 0.5,
        ),
        size=(DOOR_WIDTH, cutter_depth, DOOR_HEIGHT),
    )
    boolean_difference(body, cutter)
    # Also cut the façade cladding overlay.
    facade = bpy.data.objects.get("crypt_facade_cladding")
    if facade is not None:
        cutter2 = add_box(
            name="_door_cutter_facade",
            center=(
                0.0,
                -BODY_LENGTH * 0.5 - 0.05,
                PLINTH_HEIGHT + DOOR_HEIGHT * 0.5,
            ),
            size=(DOOR_WIDTH, 0.5, DOOR_HEIGHT),
        )
        boolean_difference(facade, cutter2)


def cut_oculus():
    """Cut the round oculus (rosone) in the upper front wall slab."""
    upper = bpy.data.objects.get("crypt_wall_front_upper")
    if upper is not None:
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=OCULUS_SEGMENTS,
            radius=OCULUS_RADIUS,
            depth=1.0,
            location=(0.0, -BODY_LENGTH * 0.5, OCULUS_CENTER_Z),
            rotation=(math.pi * 0.5, 0.0, 0.0),
        )
        cutter = bpy.context.active_object
        cutter.name = "_oculus_cutter"
        boolean_difference(upper, cutter)
    facade_upper = bpy.data.objects.get("crypt_facade_cladding_upper")
    if facade_upper is not None:
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=OCULUS_SEGMENTS,
            radius=OCULUS_RADIUS,
            depth=0.5,
            location=(0.0, -BODY_LENGTH * 0.5, OCULUS_CENTER_Z),
            rotation=(math.pi * 0.5, 0.0, 0.0),
        )
        cutter2 = bpy.context.active_object
        cutter2.name = "_oculus_cutter_facade"
        boolean_difference(facade_upper, cutter2)


def build_apse():
    """Semicircular projection on the rear short wall, built as a
    half-cylinder. A doorway-sized tunnel is cut through it at the
    chapel-back-wall axis so the descent stair shaft can pass through
    the apse footprint into the landing area beyond. Without the cut,
    the solid half-cylinder of stone blocks the player's view through
    the back-wall tunnel into the descent at eye level."""
    # Apse cylinder position + cutter dimensions all derived from JOINT.
    # Three constraints satisfied by construction:
    #   1. Cylinder front face buried inside back-wall (apse_back_wall_inset)
    #   2. Tunnel cutout X exactly matches back-wall tunnel cutout
    #      (corridor_opening_half_x — shared between both)
    #   3. Tunnel cutout TOP clears the corridor ceiling top with
    #      z_fight_safety margin (apse_doorway_top_chapel_local_z)
    apse_cy = apse_center_y()
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=APSE_SEGMENTS,
        radius=APSE_RADIUS,
        depth=APSE_HEIGHT,
        location=(0.0, apse_cy, APSE_HEIGHT * 0.5),
    )
    apse = bpy.context.active_object
    apse.name = "crypt_apse"

    half_cutter = add_box(
        name="_apse_half_cutter",
        center=(0.0, apse_cy - APSE_RADIUS, APSE_HEIGHT * 0.5),
        size=(APSE_RADIUS * 4, APSE_RADIUS * 2, APSE_HEIGHT + 1.0),
    )
    boolean_difference(apse, half_cutter)

    # Stair tunnel cut: removes ONLY the back-wall doorway band, NOT
    # the entire region below the corridor. The corridor descends
    # below the apse cylinder (the apse sits on terrain; corridor goes
    # UNDERGROUND beneath it), so the apse keeps its full curved face
    # from chapel-floor down to terrain. Cutter Y range matches the
    # back-wall tunnel opening: from PLINTH_HEIGHT (chapel floor) up
    # to apse_doorway_top_chapel_local_z (shared with back-wall).
    tunnel_top_z = apse_doorway_top_chapel_local_z(
        continuous_descent_first_tread_z())
    tunnel_bottom_z = PLINTH_HEIGHT  # chapel floor — apse keeps material below
    tunnel_center_z = (tunnel_top_z + tunnel_bottom_z) * 0.5
    tunnel_height = tunnel_top_z - tunnel_bottom_z
    apse_tunnel_cutter = add_box(
        name="_apse_tunnel_cutter",
        center=(0.0, apse_cy + APSE_RADIUS * 0.5, tunnel_center_z),
        size=(2.0 * corridor_opening_half_x(),
              APSE_RADIUS * 2 + 1.0,
              tunnel_height),
    )
    boolean_difference(apse, apse_tunnel_cutter)

    if MAT_PIENZA is not None:
        apse.data.materials.append(MAT_PIENZA)
    return apse


def build_apse_roof():
    """Conical / half-conical cap over the apse."""
    apse_roof_height = PEDIMENT_HEIGHT
    apse_cy = apse_center_y()
    bpy.ops.mesh.primitive_cone_add(
        vertices=APSE_SEGMENTS,
        radius1=APSE_RADIUS,
        radius2=0.0,
        depth=apse_roof_height,
        location=(0.0, apse_cy, APSE_HEIGHT + apse_roof_height * 0.5),
    )
    cone = bpy.context.active_object
    cone.name = "crypt_apse_roof"

    half_cutter = add_box(
        name="_apse_roof_cutter",
        center=(0.0, apse_cy - APSE_RADIUS, APSE_HEIGHT + apse_roof_height * 0.5),
        size=(APSE_RADIUS * 4, APSE_RADIUS * 2, apse_roof_height + 0.4),
    )
    boolean_difference(cone, half_cutter)
    if MAT_TERRACOTTA is not None:
        cone.data.materials.append(MAT_TERRACOTTA)
    return cone




def build_cross_at_apex():
    """Inverted iron cross at the apex of the FRONT pediment.

    Petrine cross: post upright, arm sitting low (~30% up the post) so
    the long part rises and the short part is near the base.
    """
    apex_z = BODY_HEIGHT_TO_CORNICE + PEDIMENT_HEIGHT
    apex_y = -BODY_LENGTH * 0.5

    post = add_box(
        name="crypt_cross_post",
        center=(0.0, apex_y, apex_z + CROSS_HEIGHT * 0.5),
        size=(CROSS_THICKNESS, CROSS_THICKNESS, CROSS_HEIGHT),
        material=MAT_IRON,
    )
    arm = add_box(
        name="crypt_cross_arm",
        center=(0.0, apex_y, apex_z + CROSS_HEIGHT * 0.3),
        size=(CROSS_ARM_LENGTH * 2.0, CROSS_THICKNESS, CROSS_THICKNESS),
        material=MAT_IRON,
    )
    return post, arm


# ---------------------------------------------------------------------------
# Descent stair: two flanking upper flights → landing → straight corridor
# down to Limbo platform. Carved out of stone / colle interior.
# All coords below are chapel-local (X lateral, Y forward-back with apse
# at +Y, Z up; chapel floor at Z=PLINTH_HEIGHT).
# ---------------------------------------------------------------------------

def cut_stair_holes():
    """No-op. The hole is now formed by build_plinth authoring four
    slabs around the hole's footprint (see build_plinth's docstring).
    Kept as a stub so build_descent's call site stays unchanged."""


def build_upper_flight():
    """Solid descending staircase spanning the chapel interior width.
    Each step is a FULL BLOCK from its tread surface DOWN to a common
    base below the lowest step — so the staircase is one continuous
    solid with a stepped top surface, not 9 floating thin slabs.

    Real-physics doctrine: author geometry that IS what it depicts
    (a stair is a solid). Don't rely on physics tuning to "make
    floating slabs behave like stairs" — the floating slabs caused
    CharacterVirtual to fall through gaps between treads, with the
    capsule landing on terrain below.
    """
    flight_drop = UPPER_FLIGHT_STEP_COUNT * STAIR_RISE
    # Common base of all step blocks. One full rise BELOW the lowest
    # step's tread, so blocks visually extend down into the landing
    # area below — they meet the landing top from above. Side walls
    # (build_upper_flight_enclosure) close the gap below the bottom.
    base_z = PLINTH_HEIGHT - flight_drop - STAIR_RISE
    # Same step_side_inset rationale as the continuous descent — pull
    # step X faces inside the upper-flight shaft wall inner faces.
    upper_step_half_w = SINGLE_FLIGHT_HALF_WIDTH - 0.005
    for i in range(UPPER_FLIGHT_STEP_COUNT):
        step_top_z = PLINTH_HEIGHT - (i + 1) * STAIR_RISE
        block_height = step_top_z - base_z
        block_center_z = (step_top_z + base_z) * 0.5
        step_center_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * (i + 0.5) * STAIR_TREAD
        add_box(
            name=f"stair_upper_step_{i:02d}",
            center=(0.0, step_center_y, block_center_z),
            size=(2.0 * upper_step_half_w, STAIR_TREAD, block_height),
            material=MAT_PIENZA_DARK,
        )


def build_upper_flight_enclosure():
    """Close the upper flight's side + back walls so the player
    descending the stairs sees a real tunnel, not a roofless trench
    with the chapel interior visible above. Without these walls, the
    sides of the stair shaft are open all the way to the chapel side
    walls (which start ABOVE the plinth at Z=PLINTH_HEIGHT), creating
    a horizontal gap that lets terrain / outdoor light bleed through
    near the steps. The back wall closes the apse-side end of the
    shaft below the chapel back wall."""
    flight_drop = UPPER_FLIGHT_STEP_COUNT * STAIR_RISE
    flight_length = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD
    # Shaft Z range: from BELOW the lowest step (with a bit of margin)
    # up to the plinth bottom (the underside of the chapel floor that
    # forms the shaft's ceiling).
    shaft_bottom_z = PLINTH_HEIGHT - flight_drop - 0.5
    shaft_top_z = 0.0  # = plinth bottom; chapel floor is the ceiling
    shaft_height = shaft_top_z - shaft_bottom_z
    shaft_center_z = (shaft_top_z + shaft_bottom_z) * 0.5

    wall_thickness = 0.30

    # Side walls — left + right. Sit OUTSIDE the steps' X extent
    # (X = ±SINGLE_FLIGHT_HALF_WIDTH = ±2.4) so they don't clip into
    # the stair treads. Wall outer face = chapel interior wall face
    # so the chapel wall above (which starts at Z=PLINTH_HEIGHT) sits
    # flush atop the shaft wall — vertically continuous stone face.
    for sign in (-1, 1):
        add_box(
            name=f"stair_upper_wall_{'r' if sign > 0 else 'l'}",
            center=(sign * (SINGLE_FLIGHT_HALF_WIDTH + wall_thickness * 0.5),
                    UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * flight_length * 0.5,
                    shaft_center_z),
            size=(wall_thickness, flight_length, shaft_height),
            material=MAT_DESCENT_STONE,
        )

    # NO back wall on the upper flight shaft — the descent continues
    # through the chapel back wall's stair tunnel (see crypt_wall_back_*
    # in build_body) into the landing beyond. Adding a back wall here
    # would dead-end the descent against a closed face.



def _build_wedge_ramp(name, center_x, center_y,
                      top_y_high, top_z_high,
                      top_y_low, top_z_low,
                      width, thickness, material=None,
                      invert_normals=False):
    """Sloped slab whose TOP goes from (y_high, z_high) down to
    (y_low, z_low); bottom mirrors below by `thickness`. center_x/y
    are unused (positions come from the y_high/y_low parameters);
    kept in the signature for parity with `add_box`-style call sites."""
    del center_x, center_y  # unused
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    obj = bpy.data.objects.new(name, mesh)
    _link_to_current(obj)
    bm = bmesh.new()
    half_w = width * 0.5
    v_top_hi_l = bm.verts.new((-half_w, top_y_high, top_z_high))
    v_top_hi_r = bm.verts.new(( half_w, top_y_high, top_z_high))
    v_top_lo_l = bm.verts.new((-half_w, top_y_low,  top_z_low))
    v_top_lo_r = bm.verts.new(( half_w, top_y_low,  top_z_low))
    v_bot_hi_l = bm.verts.new((-half_w, top_y_high, top_z_high - thickness))
    v_bot_hi_r = bm.verts.new(( half_w, top_y_high, top_z_high - thickness))
    v_bot_lo_l = bm.verts.new((-half_w, top_y_low,  top_z_low  - thickness))
    v_bot_lo_r = bm.verts.new(( half_w, top_y_low,  top_z_low  - thickness))
    if not invert_normals:
        bm.faces.new([v_top_hi_l, v_top_hi_r, v_top_lo_r, v_top_lo_l])
        bm.faces.new([v_bot_lo_l, v_bot_lo_r, v_bot_hi_r, v_bot_hi_l])
        bm.faces.new([v_top_hi_l, v_top_lo_l, v_bot_lo_l, v_bot_hi_l])
        bm.faces.new([v_top_lo_r, v_top_hi_r, v_bot_hi_r, v_bot_lo_r])
        bm.faces.new([v_top_hi_r, v_top_hi_l, v_bot_hi_l, v_bot_hi_r])
        bm.faces.new([v_top_lo_l, v_top_lo_r, v_bot_lo_r, v_bot_lo_l])
    else:
        bm.faces.new([v_top_lo_l, v_top_lo_r, v_top_hi_r, v_top_hi_l])
        bm.faces.new([v_bot_hi_l, v_bot_hi_r, v_bot_lo_r, v_bot_lo_l])
        bm.faces.new([v_bot_hi_l, v_bot_lo_l, v_top_lo_l, v_top_hi_l])
        bm.faces.new([v_bot_lo_r, v_bot_hi_r, v_top_hi_r, v_top_lo_r])
        bm.faces.new([v_bot_hi_r, v_bot_hi_l, v_top_hi_l, v_top_hi_r])
        bm.faces.new([v_bot_lo_l, v_bot_lo_r, v_top_lo_r, v_top_lo_l])
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    if material is not None:
        obj.data.materials.append(material)
    return obj


def _build_wedge_wall(name, center_x, y_high, z_high_floor, y_low, z_low_floor,
                      thickness, wall_height, material=None):
    """Vertical wall whose floor edge follows a sloped line from
    (y_high, z_high_floor) down to (y_low, z_low_floor); top edge
    sits wall_height above the floor at each Y."""
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    obj = bpy.data.objects.new(name, mesh)
    _link_to_current(obj)
    bm = bmesh.new()
    half_t = thickness * 0.5
    v_fl_n_a = bm.verts.new((center_x - half_t, y_high, z_high_floor))
    v_fl_n_b = bm.verts.new((center_x + half_t, y_high, z_high_floor))
    v_fl_p_a = bm.verts.new((center_x - half_t, y_low,  z_low_floor))
    v_fl_p_b = bm.verts.new((center_x + half_t, y_low,  z_low_floor))
    v_tp_n_a = bm.verts.new((center_x - half_t, y_high, z_high_floor + wall_height))
    v_tp_n_b = bm.verts.new((center_x + half_t, y_high, z_high_floor + wall_height))
    v_tp_p_a = bm.verts.new((center_x - half_t, y_low,  z_low_floor  + wall_height))
    v_tp_p_b = bm.verts.new((center_x + half_t, y_low,  z_low_floor  + wall_height))
    bm.faces.new([v_tp_n_a, v_tp_n_b, v_tp_p_b, v_tp_p_a])  # top
    bm.faces.new([v_fl_p_a, v_fl_p_b, v_fl_n_b, v_fl_n_a])  # bottom
    bm.faces.new([v_fl_n_a, v_fl_n_b, v_tp_n_b, v_tp_n_a])  # high end
    bm.faces.new([v_fl_p_b, v_fl_p_a, v_tp_p_a, v_tp_p_b])  # low end
    bm.faces.new([v_fl_n_b, v_fl_p_b, v_tp_p_b, v_tp_n_b])  # +X face
    bm.faces.new([v_fl_p_a, v_fl_n_a, v_tp_n_a, v_tp_p_a])  # -X face
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    if material is not None:
        obj.data.materials.append(material)
    return obj


def build_continuous_descent():
    """One uniform staircase from the bottom of the upper flight all
    the way down to acheron, plus matching tunnel walls and ceiling
    running the full length. Step proportions match the upper flight
    (STAIR_RISE / STAIR_TREAD) so the player descends at a steady
    pace with no transitions, landings, or slope changes.

    Real-physics doctrine: uniform stairs all the way down means
    CharacterVirtual's step-down behavior fires identically at every
    step. No drop-feel artifacts at landing transitions, no slope
    angles above the controller's max-slope threshold.
    """
    upper_flight_drop = UPPER_FLIGHT_STEP_COUNT * STAIR_RISE
    upper_flight_run = UPPER_FLIGHT_STEP_COUNT * STAIR_TREAD
    # Top of the continuous descent meets the bottom of the upper flight.
    descent_top_z = PLINTH_HEIGHT - upper_flight_drop  # chapel-local Z
    descent_top_y = UPPER_FLIGHT_TOP_Y + DESCENT_FORWARD_SIGN * upper_flight_run

    # Each step is a thin slab of height STAIR_RISE (plus a small
    # downward overlap so the visible riser of step i overlaps the
    # tread of step i+1 — no z-fighting at the riser/tread seam).
    # Previously authored as solid blocks extending from each tread
    # to a shared base far below; that made step 0's block 64m tall
    # and visually swallowed every step below it, producing the
    # appearance of a single ramp instead of discrete stairs.
    final_step_top_z = descent_top_z - CONTINUOUS_DESCENT_STEP_COUNT * STAIR_RISE
    riser_overlap = 0.01  # 1cm — eliminates seam crack without z-fight

    half_w = SINGLE_FLIGHT_HALF_WIDTH
    # Pull step X faces 5mm inside the corridor walls so step sides
    # at x=±(half_w - inset) are NOT coplanar with wall inner faces
    # at x=±half_w. Coplanar surfaces along the entire 140m descent
    # caused per-pixel flicker on every step's side face.
    step_side_inset = 0.005
    step_half_w = half_w - step_side_inset

    # ---- Step blocks (visual silhouette only) ----
    # All N step slabs collapse into ONE Blender mesh / ONE glTF node /
    # ONE GL VAO / ZERO Jolt shapes. Player physics walks the smooth
    # ramp authored below; visible stairs are decoupled from collision
    # — standard FromSoft pattern. This is the "real" geometry for the
    # eye; the ramp under it is the "real" geometry for the capsule.
    # See feedback_dual_source_of_truth_is_the_bug: one Python source
    # (the descent parameters) produces both outputs in lockstep, so
    # they cannot drift.
    step_specs = []
    for i in range(CONTINUOUS_DESCENT_STEP_COUNT):
        step_top_z = descent_top_z - (i + 1) * STAIR_RISE
        step_bot_z = step_top_z - STAIR_RISE - riser_overlap
        block_height = step_top_z - step_bot_z
        block_center_z = (step_top_z + step_bot_z) * 0.5
        step_center_y = descent_top_y + DESCENT_FORWARD_SIGN * (i + 0.5) * STAIR_TREAD
        step_specs.append((
            (0.0, step_center_y, block_center_z),
            (2.0 * step_half_w, STAIR_TREAD, block_height),
        ))
    add_merged_boxes(
        name='descent_steps_visual',
        box_specs=step_specs,
        material=MAT_DESCENT_STONE,
        usage='visual',
    )

    # ---- Collision ramp: one inclined slab spanning the full descent
    # at the average tread Y. Player capsule walks this smooth surface
    # — no per-step Y jump → no camera bob. Slab is thin (2cm) so it
    # sits flush under the visual stairs without poking through the
    # treads. Top face follows the line from the first step's tread
    # top to the last step's tread top.
    ramp_top_z_start = descent_top_z - STAIR_RISE        # first tread top Z (chapel-local)
    ramp_top_z_end   = descent_top_z - CONTINUOUS_DESCENT_STEP_COUNT * STAIR_RISE  # last tread top Z
    ramp_y_start     = descent_top_y + DESCENT_FORWARD_SIGN * 0.5 * STAIR_TREAD
    ramp_y_end       = descent_top_y + DESCENT_FORWARD_SIGN * (CONTINUOUS_DESCENT_STEP_COUNT - 0.5) * STAIR_TREAD
    ramp_thickness   = 0.02  # 2cm slab; bottom face buried under steps
    rhx = step_half_w        # match stair half-width
    # 8 verts of the inclined slab (top + bottom face along the slope).
    # Y/Z pairs are interpolated; X is ±rhx.
    ramp_verts = [
        (-rhx, ramp_y_start, ramp_top_z_start),                 # 0: top start L
        ( rhx, ramp_y_start, ramp_top_z_start),                 # 1: top start R
        ( rhx, ramp_y_end,   ramp_top_z_end),                   # 2: top end R
        (-rhx, ramp_y_end,   ramp_top_z_end),                   # 3: top end L
        (-rhx, ramp_y_start, ramp_top_z_start - ramp_thickness),# 4: bot start L
        ( rhx, ramp_y_start, ramp_top_z_start - ramp_thickness),# 5: bot start R
        ( rhx, ramp_y_end,   ramp_top_z_end   - ramp_thickness),# 6: bot end R
        (-rhx, ramp_y_end,   ramp_top_z_end   - ramp_thickness),# 7: bot end L
    ]
    ramp_faces = [
        (0, 1, 2, 3),  # top (player walks this face)
        (4, 7, 6, 5),  # bottom
        (0, 4, 5, 1),  # -Y start cap
        (3, 2, 6, 7),  # +Y end cap
        (0, 3, 7, 4),  # -X side
        (1, 5, 6, 2),  # +X side
    ]
    add_mesh_from_pydata(
        name='descent_collision_ramp',
        verts=ramp_verts,
        faces=ramp_faces,
        material=None,   # not rendered
        usage='collision',
    )

    # ---- Tunnel walls + ceiling: sloped wedges that FOLLOW the
    # stairs, not flat boxes. Cross-section + ceiling clearance derive
    # from JOINT so back-wall + apse + corridor all share the SAME
    # opening shape — no possibility of one surface poking through
    # another by sub-millimeter misalignment.
    wall_thickness = JOINT["corridor_wall_thickness"]
    ceiling_thickness = JOINT["corridor_ceiling_thickness"]
    tunnel_headroom = JOINT["corridor_ceiling_clearance"]

    # Tunnel floor reference for walls: each wall sits BESIDE the
    # steps, descending in parallel. Wall bottom edge tracks the
    # ACTUAL tread tops of the first and last step.
    first_tread_z = descent_top_z - STAIR_RISE       # tread of step_0000
    last_tread_z  = descent_top_z - CONTINUOUS_DESCENT_STEP_COUNT * STAIR_RISE
    first_tread_y = descent_top_y + DESCENT_FORWARD_SIGN * 0.5 * STAIR_TREAD
    last_tread_y  = descent_top_y + DESCENT_FORWARD_SIGN * (CONTINUOUS_DESCENT_STEP_COUNT - 0.5) * STAIR_TREAD
    # Wall extends from z_floor (passed in via z_high_floor/z_low_floor,
    # which is already 0.5 BELOW the actual stair tread for visual depth)
    # up by wall_height. Top is capped at apse_doorway_top - zfs so the
    # wall doesn't poke into chapel-back-upper's Z range (which starts at
    # back_tunnel_top_z = apse_doorway_top). Computed at the wall's CHAPEL-
    # SIDE end (steepest end of the wedge) so the cap holds for the entire
    # wedge run; back-side end is naturally lower along the slope.
    apse_doorway_top_z = apse_doorway_top_chapel_local_z(
        continuous_descent_first_tread_z())
    # wall_start_z + wall_height - 0.5 ≤ apse_doorway_top_z - zfs
    # → wall_height ≤ apse_doorway_top_z - wall_start_z + 0.5 - zfs
    # wall_start_z depends on apse-far interpolation; compute below.

    # Walls FIRST APPEAR past the apse cylinder so no descent-wall
    # surface sits inside apse material. JOINT helper derives the Y
    # position from apse_far_y + z_fight_safety.
    wall_start_y = descent_wall_first_appearance_y()
    # Interpolate floor Z at the wall's new start point (linear between
    # first_tread and last_tread along the descent slope).
    descent_slope_run = last_tread_y - first_tread_y
    descent_slope_drop = last_tread_z - first_tread_z
    wall_start_z = first_tread_z + (wall_start_y - first_tread_y) * (descent_slope_drop / descent_slope_run)
    # Cap wall height so its top stays below apse_doorway_top (= chapel
    # back wall's tunnel-opening top, which is where crypt_wall_back_upper
    # starts). zfs intrusion margin below that.
    wall_height = (apse_doorway_top_z - wall_start_z) + 0.5 - JOINT["z_fight_safety"]
    for sign in (-1, 1):
        _build_wedge_wall(
            name=f"descent_wall_{'r' if sign > 0 else 'l'}",
            center_x=sign * (JOINT["corridor_half_w"] + wall_thickness * 0.5),
            y_high=wall_start_y, z_high_floor=wall_start_z - 0.5,
            y_low=last_tread_y,  z_low_floor=last_tread_z - 0.5,
            thickness=wall_thickness,
            wall_height=wall_height,
            material=MAT_DESCENT_STONE,
        )

    # Ceiling: sloped slab whose UNDERSIDE sits tunnel_headroom above
    # the stair tread line, parallel to the descent slope. Player
    # always has the same headroom regardless of depth.
    # Ceiling truncates at wall_start_y on the chapel-side end (same
    # as the walls) so it doesn't extend into the apse interior.
    ceiling_y_high = wall_start_y
    ceiling_z_high_floor = wall_start_z  # already interpolated above
    ceiling_under_z_high = ceiling_z_high_floor + tunnel_headroom
    ceiling_under_z_low  = last_tread_z  + tunnel_headroom
    # Ceiling extends slightly INTO each wall (5mm past wall inner
    # face on each side) so the ceiling's outer X edge is buried
    # inside the wall material — not coplanar with the wall outer
    # face at x=±corridor_outer_half_x, which caused visible z-fight
    # flicker on the corridor wall outer face along the descent.
    ceiling_edge_inset = 0.005
    _build_wedge_ramp(
        name='descent_ceiling',
        center_x=0.0, center_y=0.0,
        top_y_high=ceiling_y_high,
        top_z_high=ceiling_under_z_high + ceiling_thickness,
        top_y_low=last_tread_y,
        top_z_low=ceiling_under_z_low + ceiling_thickness,
        width=2.0 * (JOINT["corridor_half_w"] + ceiling_edge_inset),
        thickness=ceiling_thickness,
        material=MAT_DESCENT_STONE,
        invert_normals=False,
    )

    tunnel_length_y = CONTINUOUS_DESCENT_STEP_COUNT * STAIR_TREAD
    return (descent_top_y + DESCENT_FORWARD_SIGN * tunnel_length_y,  # limbo_entry_y
            final_step_top_z)                                          # limbo_top_z


def build_descent():
    """Top-level: cut hole, build the upper flight inside the chapel,
    then a uniform continuous staircase from there all the way down
    to where Limbo begins. The Limbo plain itself is its own terrain
    region (assets/world/terrain/config.json: limbo) — the descent
    corridor stops at the bottom of its final step, the Limbo terrain
    picks up from there.
    """
    cut_stair_holes()
    build_upper_flight()
    build_upper_flight_enclosure()
    build_continuous_descent()


# ---------------------------------------------------------------------------
# Save
# ---------------------------------------------------------------------------

def render_plan_view(name_prefixes, out_png):
    """Render a top-down (XY-plane) plan view PNG of every matching
    object's AABB, with labels. Lets us see geometry layout without
    launching the game. Z is used only to color-code: higher = brighter.
    Origin at image center; +X right, +Y up (Blender convention)."""
    import struct
    import zlib

    boxes = []
    for obj in sorted(bpy.data.objects, key=lambda o: o.name):
        if not any(obj.name.startswith(p) for p in name_prefixes):
            continue
        if obj.type != "MESH" or not obj.data.vertices:
            continue
        xs, ys, zs = [], [], []
        for v in obj.data.vertices:
            wp = obj.matrix_world @ v.co
            xs.append(wp.x); ys.append(wp.y); zs.append(wp.z)
        boxes.append((obj.name, min(xs), max(xs), min(ys), max(ys),
                      min(zs), max(zs)))
    if not boxes:
        print(f"[plan_view] no boxes matched {name_prefixes}")
        return

    pad = 1.0
    all_x = [b[1] for b in boxes] + [b[2] for b in boxes]
    all_y = [b[3] for b in boxes] + [b[4] for b in boxes]
    x_min, x_max = min(all_x) - pad, max(all_x) + pad
    y_min, y_max = min(all_y) - pad, max(all_y) + pad

    scale = 60.0   # px per meter
    img_w = int((x_max - x_min) * scale)
    img_h = int((y_max - y_min) * scale)

    pixels = bytearray([20, 20, 20] * (img_w * img_h))

    def to_px(x, y):
        px = int((x - x_min) * scale)
        py = int((y_max - y) * scale)
        return px, py

    def hline(y, x0, x1, color):
        if y < 0 or y >= img_h: return
        x0 = max(0, min(img_w - 1, x0))
        x1 = max(0, min(img_w - 1, x1))
        if x0 > x1: x0, x1 = x1, x0
        for x in range(x0, x1 + 1):
            i = (y * img_w + x) * 3
            pixels[i:i+3] = bytes(color)

    def vline(x, y0, y1, color):
        if x < 0 or x >= img_w: return
        y0 = max(0, min(img_h - 1, y0))
        y1 = max(0, min(img_h - 1, y1))
        if y0 > y1: y0, y1 = y1, y0
        for y in range(y0, y1 + 1):
            i = (y * img_w + x) * 3
            pixels[i:i+3] = bytes(color)

    for name, x0w, x1w, y0w, y1w, z0w, z1w in boxes:
        if name.startswith("crypt_plinth"):
            color = (90, 200, 120)
        elif name.startswith("crypt_wall"):
            color = (90, 140, 220)
        elif name.startswith("stair_upper_step"):
            color = (220, 160, 90)
        else:
            color = (200, 200, 200)
        p0x, p0y = to_px(x0w, y0w)
        p1x, p1y = to_px(x1w, y1w)
        hline(p0y, p0x, p1x, color)
        hline(p1y, p0x, p1x, color)
        vline(p0x, p0y, p1y, color)
        vline(p1x, p0y, p1y, color)

    cx, cy = to_px(0, 0)
    for d in range(-8, 9):
        if 0 <= cx + d < img_w and 0 <= cy < img_h:
            i = (cy * img_w + (cx + d)) * 3
            pixels[i:i+3] = bytes((255, 80, 80))
        if 0 <= cy + d < img_h and 0 <= cx < img_w:
            i = ((cy + d) * img_w + cx) * 3
            pixels[i:i+3] = bytes((255, 80, 80))

    raw = bytearray()
    for y in range(img_h):
        raw.append(0)
        raw.extend(pixels[y * img_w * 3:(y + 1) * img_w * 3])
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", img_w, img_h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw)))
    png += chunk(b"IEND", b"")
    with open(out_png, "wb") as f:
        f.write(png)
    print(f"[plan_view] wrote {out_png}  ({img_w}x{img_h}, {len(boxes)} boxes)")
    print(f"[plan_view] view bounds: X=[{x_min:.2f}, {x_max:.2f}]  "
          f"Y=[{y_min:.2f}, {y_max:.2f}]  origin = red cross")
    print(f"[plan_view] colors: green=plinth  blue=wall  orange=stair_step")


def dump_aabbs(name_prefixes):
    """Print object AABBs (min/max in world coords) for every object
    whose name starts with one of `name_prefixes`. Used to diagnose
    plinth/wall/step geometry without guessing — read the numbers,
    don't reason about them in your head."""
    print("[gen_crypt_foundation] --- AABB DUMP ---")
    for obj in sorted(bpy.data.objects, key=lambda o: o.name):
        if not any(obj.name.startswith(p) for p in name_prefixes):
            continue
        if obj.type != "MESH":
            continue
        xs = []
        ys = []
        zs = []
        for v in obj.data.vertices:
            wp = obj.matrix_world @ v.co
            xs.append(wp.x)
            ys.append(wp.y)
            zs.append(wp.z)
        if not xs:
            continue
        print(f"  {obj.name:32s}  "
              f"X=[{min(xs):+7.3f},{max(xs):+7.3f}]  "
              f"Y=[{min(ys):+7.3f},{max(ys):+7.3f}]  "
              f"Z=[{min(zs):+7.3f},{max(zs):+7.3f}]")
    print("[gen_crypt_foundation] --- END DUMP ---")


def save_blend(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path)
    print(f"[gen_crypt_foundation] wrote {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", "..", "..", ".."))
    output_path = os.path.join(repo_root, OUTPUT_REL)

    reset_scene()
    init_materials()

    # Chapel exterior: anything visible from outside the chapel building
    # (facade, roof, walls, plinth/skirt, door threshold, cross). The
    # plinth has the descent shaft hole in it, but the plinth itself is
    # exterior masonry, so it belongs here.
    with current_collection("chapel_exterior"):
        build_foundation_skirt()
        build_plinth()
        build_door_threshold()
        build_body()
        build_facade_overlay()
        build_cornice()
        build_corner_pilasters()
        build_pediment()
        build_roof()
        # Apse + apse_roof disabled. Chapel ends at flat back wall; the
        # back-wall tunnel opens into the descent corridor directly. No
        # curved projection. Restore by uncommenting + redoing the
        # apse_tunnel_cutter sizing if/when we want it back.
        # build_apse()
        # build_apse_roof()
        build_cross_at_apex()
        # Oculus is the only boolean cut left -- applied to the upper
        # front-wall slab (a chapel_exterior object). The door is a
        # constructed gap between two solid front-wall slabs; no
        # boolean needed. Run inside chapel_exterior so the temporary
        # cutter primitive (even though it gets deleted) is parented
        # to the same collection as the slab it cuts.
        cut_oculus()

    # Chapel interior: floor that closes the chapel from inside, plus
    # the entire descent staircase down into Limbo. Player crosses
    # the chapel-door trigger to enter the chapel_interior region; the
    # whole descent walk happens inside that region until they cross
    # the bottom-of-stairs trigger into limbo. Per Commit 3 design
    # lock + docs/design/setting.md vertical-stack doctrine.
    with current_collection("chapel_interior"):
        build_interior_floor()
        # Descent stair. Runs AFTER cut_oculus so the boolean cuts on
        # crypt_floor / crypt_plinth happen against the final-form
        # chapel geometry. Limbo is its own terrain region
        # (config.json: limbo).
        build_descent()

    dump_aabbs(["crypt_courtyard", "crypt_foundation_skirt", "crypt_floor",
                "crypt_plinth", "crypt_wall", "crypt_apse", "stair_upper"])

    plan_png = os.path.join(repo_root, "games/selva-oscura/.traces/crypt_plan.png")
    os.makedirs(os.path.dirname(plan_png), exist_ok=True)
    render_plan_view(["crypt_plinth", "crypt_wall", "stair_upper_step"], plan_png)

    save_blend(output_path)


if __name__ == "__main__":
    main()
