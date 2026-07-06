"""Reusable stair geometry primitives.

Two flavors, both authored in Blender-local X/Y/Z with an explicit
travel direction vector so the same code produces flights along any
axis-aligned direction (chapel descends in +Y; a castle segment might
travel in +X, -X, +Y, or -Y depending on which side of the ring it is
on).

## build_solid_flight

Short flights of solid step blocks. Each step is a full box from its
tread top down to a shared base Z below the lowest step. Player
physics walks the block tops directly (no separate collision proxy).
Use when the flight is a handful of steps and the physics interaction
should be exactly the visible geometry — chapel's 9-step upper flight
between plinth top and continuous-descent start.

## build_stepped_ramp

Long flights that visually read as discrete steps but present a
smooth collision surface. All N visible step slabs are merged into
ONE mesh with usage='visual'; a single tilted slab spans the whole
flight with usage='collision'. Player capsule walks the smooth ramp
— no per-step camera bob, no risk of falling through gaps — while
the eye sees discrete stairs. Standard FromSoft pattern; verified
in production on the chapel's continuous descent (400 steps, 64m
drop). Use when the flight is long enough that per-step physics
would produce visible bob.

## Coordinate convention

- `origin` is the (x, y, z) of the FIRST step's tread top-center. The
  flight extends forward from there in the direction of `travel`.
- `travel` is a unit-length (dx, dy) in Blender's XY plane. Currently
  restricted to axis-aligned — one component must be exactly 0 and
  the other exactly ±1. Diagonal travel is a future extension.
- `ascending=False` (default) descends: each subsequent tread top is
  STAIR_RISE lower than the previous. `ascending=True` rises.
- `width` is the flight's cross-axis span (perpendicular to travel).
"""

from .mesh_primitives import add_box, add_merged_boxes, add_mesh_from_pydata


def _resolve_axes(travel):
    """Given a unit-length axis-aligned (dx, dy), return (forward_axis,
    forward_sign, cross_axis). forward_axis is 'x' or 'y'; cross_axis
    is the other one. forward_sign is +1 or -1."""
    dx, dy = travel
    if dx != 0.0 and dy != 0.0:
        raise ValueError(f"stair_flight: diagonal travel not supported (got {travel!r})")
    if dx == 0.0 and dy == 0.0:
        raise ValueError("stair_flight: travel vector must be non-zero")
    if abs(dx) == 1.0 and dy == 0.0:
        return ("x", int(dx), "y")
    if dx == 0.0 and abs(dy) == 1.0:
        return ("y", int(dy), "x")
    raise ValueError(
        f"stair_flight: travel components must be exactly 0 or ±1 (got {travel!r})")


def _center_from_axes(forward_axis, cross_axis, forward_pos, cross_pos, z, origin):
    """Compose an (x, y, z) center given a forward/cross-axis split."""
    ox, oy, _ = origin
    if forward_axis == "y":
        return (ox + cross_pos, oy + forward_pos, z)
    return (ox + forward_pos, oy + cross_pos, z)


def _size_from_axes(forward_axis, forward_len, cross_len, height):
    """Compose an (sx, sy, sz) size for a box whose long axis is
    forward_axis and short axis is the cross axis."""
    if forward_axis == "y":
        return (cross_len, forward_len, height)
    return (forward_len, cross_len, height)


def build_solid_flight(origin, travel, step_count, rise, tread, width,
                       material=None, name_prefix="stair_step",
                       ascending=False, base_drop_extra=None):
    """Author `step_count` solid step blocks starting at `origin` and
    proceeding in `travel`. Each block extends from its tread top DOWN
    to a common base Z below the lowest step so the flight is one
    continuous solid with a stepped top — no floating slabs, no gaps.

    Player physics walks the block tops directly. Use for short
    flights where per-step physics is fine (~10 steps typical).

    Args:
      origin: (x, y, z) of the FIRST step's tread top-center.
      travel: axis-aligned unit vector (dx, dy) — see module docstring.
      step_count: number of discrete steps to author.
      rise: vertical distance per step (Blender Z units).
      tread: horizontal distance per step (along travel).
      width: cross-axis span of every step (perpendicular to travel).
      material: Blender material to attach to each block, or None.
      name_prefix: object naming; blocks are `{name_prefix}_{i:02d}`.
      ascending: False = each subsequent tread lower (default; matches
        chapel upper-flight descending in +Y); True = each higher.
      base_drop_extra: extra Z below the LOWEST tread top for the
        shared base. Defaults to `rise` (one full rise below the
        bottom) so blocks visually extend into the landing area.

    Returns: list of the created objects in step order.
    """
    forward_axis, forward_sign, cross_axis = _resolve_axes(travel)
    del cross_axis  # implicit via _size_from_axes
    if base_drop_extra is None:
        base_drop_extra = rise
    z_sign = 1 if ascending else -1
    lowest_tread_top_z = origin[2] + z_sign * step_count * rise if ascending \
        else origin[2] + z_sign * step_count * rise  # same formula either sign
    base_z = min(origin[2], lowest_tread_top_z) - base_drop_extra
    created = []
    for i in range(step_count):
        step_top_z = origin[2] + z_sign * (i + 1) * rise
        block_height = step_top_z - base_z
        block_center_z = (step_top_z + base_z) * 0.5
        forward_pos = forward_sign * (i + 0.5) * tread
        cross_pos = 0.0
        center = _center_from_axes(forward_axis, None,
                                   forward_pos, cross_pos, block_center_z, origin)
        size = _size_from_axes(forward_axis, tread, width, block_height)
        obj = add_box(
            name=f"{name_prefix}_{i:02d}",
            center=center,
            size=size,
            material=material,
        )
        created.append(obj)
    return created


def build_tilted_slab(name, start_center, end_center, travel, width,
                      thickness, material=None, usage=None):
    """Author a single inclined rectangular slab whose top face runs
    from start_center to end_center. Cross-axis extent is `width`
    (centered on the travel line); slab thickness is `thickness`
    (extending DOWN from the top face).

    Used for both the collision surface under a build_stepped_ramp
    stair AND standalone smooth ramps (Roman-style, no visible
    steps) like the Castle's helical corridor.

    Args:
      name: Blender object/node name.
      start_center: (x, y, z) of the top face's LEADING edge midpoint
        along the travel direction.
      end_center: (x, y, z) of the top face's TRAILING edge midpoint.
        The Z values in start/end drive the slope.
      travel: axis-aligned unit (dx, dy) — see module docstring. Used
        only to resolve the cross-axis (perpendicular to travel).
      width: cross-axis span.
      thickness: Z distance the slab extends below its top face.
      material: Blender material or None.
      usage: 'visual', 'collision', 'both', or None (default 'both').
    """
    forward_axis, _, _ = _resolve_axes(travel)
    half_w = width * 0.5

    def _corner(center, cross_sign):
        if forward_axis == "y":
            return (center[0] + cross_sign * half_w, center[1], center[2])
        return (center[0], center[1] + cross_sign * half_w, center[2])

    v0 = _corner(start_center, -1)
    v1 = _corner(start_center, +1)
    v2 = _corner(end_center,   +1)
    v3 = _corner(end_center,   -1)
    v4 = (v0[0], v0[1], v0[2] - thickness)
    v5 = (v1[0], v1[1], v1[2] - thickness)
    v6 = (v2[0], v2[1], v2[2] - thickness)
    v7 = (v3[0], v3[1], v3[2] - thickness)
    faces = [
        (0, 1, 2, 3),
        (4, 7, 6, 5),
        (0, 4, 5, 1),
        (3, 2, 6, 7),
        (0, 3, 7, 4),
        (1, 5, 6, 2),
    ]
    return add_mesh_from_pydata(
        name=name,
        verts=[v0, v1, v2, v3, v4, v5, v6, v7],
        faces=faces,
        material=material,
        usage=usage,
    )


def build_tunnel_wall(name, start_center, end_center, travel, thickness,
                      wall_height, material=None, usage=None):
    """A vertical wall whose FLOOR edge runs from start_center to
    end_center (each an (x,y,z)) and whose TOP edge sits `wall_height`
    above the floor edge at every point (parallel to the sloped
    floor line). Wall thickness extends perpendicular to travel,
    ±thickness/2 from the centerline. Used to enclose a tunnel
    around a tilted corridor: one wall on each side of the ramp
    running parallel to the travel direction.

    Chapel descent uses the same construction for its sloped side
    walls (see _build_wedge_wall in gen_chapel_and_descent.py).

    Axis-generalized version: chapel wall was Y-axis-travel only;
    this handles both ±X and ±Y travel via `travel`.
    """
    forward_axis, _, _ = _resolve_axes(travel)
    half_t = thickness * 0.5

    def _wall_corner(center, perp_sign, z):
        # Perp offset direction is perpendicular to travel, in XY plane.
        # If travel is along X, perp is along Y and vice versa.
        if forward_axis == "y":
            return (center[0] + perp_sign * half_t, center[1], z)
        return (center[0], center[1] + perp_sign * half_t, z)

    # 8 verts: floor{start,end} × ±thickness, then top+wall_height.
    v_fl_s_a = _wall_corner(start_center, -1, start_center[2])
    v_fl_s_b = _wall_corner(start_center, +1, start_center[2])
    v_fl_e_a = _wall_corner(end_center,   -1, end_center[2])
    v_fl_e_b = _wall_corner(end_center,   +1, end_center[2])
    v_tp_s_a = _wall_corner(start_center, -1, start_center[2] + wall_height)
    v_tp_s_b = _wall_corner(start_center, +1, start_center[2] + wall_height)
    v_tp_e_a = _wall_corner(end_center,   -1, end_center[2]   + wall_height)
    v_tp_e_b = _wall_corner(end_center,   +1, end_center[2]   + wall_height)
    verts = [v_fl_s_a, v_fl_s_b, v_fl_e_a, v_fl_e_b,
             v_tp_s_a, v_tp_s_b, v_tp_e_a, v_tp_e_b]
    # Face winding: outward-facing normals. Consistent with chapel wedge_wall.
    faces = [
        (4, 5, 7, 6),  # top
        (2, 3, 1, 0),  # bottom (flipped so normal points down)
        (0, 1, 5, 4),  # start-end cap
        (3, 2, 6, 7),  # end-end cap
        (1, 3, 7, 5),  # +perp face
        (2, 0, 4, 6),  # -perp face
    ]
    return add_mesh_from_pydata(
        name=name,
        verts=verts,
        faces=faces,
        material=material,
        usage=usage,
    )


def build_stepped_ramp(origin, travel, step_count, rise, tread, width,
                       material=None, visual_name=None, collision_name=None,
                       ascending=False, riser_overlap=0.01,
                       ramp_thickness=0.02, step_side_inset=0.005):
    """Author a long flight as one merged visual mesh of discrete step
    slabs + one tilted collision slab spanning the whole length. The
    eye sees stairs; the physics capsule walks a smooth ramp — no
    per-step bob. Standard FromSoft pattern.

    Args (in addition to build_solid_flight's):
      visual_name: Blender object/node name for the merged visible
        step mesh. Required — becomes the glTF node name in the
        exported .glb and shows up in engine loader logs.
      collision_name: Blender object/node name for the collision
        ramp slab. Required.
      riser_overlap: extra Z the visual step slab extends below its
        tread so adjacent risers don't crack at the seam.
      ramp_thickness: Z thickness of the collision slab. Thin so its
        bottom is buried under the visible steps.
      step_side_inset: pull the visual step slabs' cross-axis faces
        this far inside `width` so they aren't coplanar with adjacent
        wall inner faces. Coplanar surfaces along a long flight cause
        per-pixel flicker at typical camera distance.

    Returns: (visual_mesh_obj, collision_ramp_obj).
    """
    if visual_name is None or collision_name is None:
        raise ValueError(
            "build_stepped_ramp: visual_name and collision_name are required")
    forward_axis, forward_sign, _ = _resolve_axes(travel)
    z_sign = 1 if ascending else -1
    half_w = width * 0.5 - step_side_inset

    # ---- Visible step slabs merged into ONE mesh ----
    step_specs = []
    for i in range(step_count):
        step_top_z = origin[2] + z_sign * (i + 1) * rise
        step_bot_z = step_top_z - rise - riser_overlap
        block_height = step_top_z - step_bot_z
        block_center_z = (step_top_z + step_bot_z) * 0.5
        forward_pos = forward_sign * (i + 0.5) * tread
        cross_pos = 0.0
        center = _center_from_axes(forward_axis, None,
                                   forward_pos, cross_pos, block_center_z, origin)
        size = _size_from_axes(forward_axis, tread, 2.0 * half_w, block_height)
        step_specs.append((center, size))
    visual_obj = add_merged_boxes(
        name=visual_name,
        box_specs=step_specs,
        material=material,
        usage="visual",
    )

    # ---- Collision ramp: tilted slab spanning the whole flight ----
    # Top face runs from FIRST tread top to LAST tread top. Bottom
    # face mirrors below by ramp_thickness. Cross-axis extent matches
    # the visible steps (half_w) so the collision doesn't stick out
    # past the visual.
    first_tread_top_z = origin[2] + z_sign * rise
    last_tread_top_z = origin[2] + z_sign * step_count * rise
    first_forward = forward_sign * 0.5 * tread
    last_forward = forward_sign * (step_count - 0.5) * tread

    def _pt(fwd, z):
        return _center_from_axes(forward_axis, None, fwd, 0.0, z, origin)

    def _corner(fwd, cross_sign, z):
        c = _pt(fwd, z)
        # Add the cross-axis half-width offset to the correct component.
        if forward_axis == "y":
            return (c[0] + cross_sign * half_w, c[1], c[2])
        return (c[0], c[1] + cross_sign * half_w, c[2])

    # 8 verts: top{start,end} × {-cross,+cross}, then bottom.
    v0 = _corner(first_forward, -1, first_tread_top_z)
    v1 = _corner(first_forward, +1, first_tread_top_z)
    v2 = _corner(last_forward,  +1, last_tread_top_z)
    v3 = _corner(last_forward,  -1, last_tread_top_z)
    v4 = (v0[0], v0[1], v0[2] - ramp_thickness)
    v5 = (v1[0], v1[1], v1[2] - ramp_thickness)
    v6 = (v2[0], v2[1], v2[2] - ramp_thickness)
    v7 = (v3[0], v3[1], v3[2] - ramp_thickness)
    ramp_faces = [
        (0, 1, 2, 3),
        (4, 7, 6, 5),
        (0, 4, 5, 1),
        (3, 2, 6, 7),
        (0, 3, 7, 4),
        (1, 5, 6, 2),
    ]
    collision_obj = add_mesh_from_pydata(
        name=collision_name,
        verts=[v0, v1, v2, v3, v4, v5, v6, v7],
        faces=ramp_faces,
        material=None,
        usage="collision",
    )
    return visual_obj, collision_obj
