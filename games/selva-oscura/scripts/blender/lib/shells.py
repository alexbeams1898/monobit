"""Cylindrical / rotational-symmetry architecture primitives.

Used for Roman-style circular structures — drum walls, apse
half-cylinders, dome interiors, columns. Author each as a
polygon approximation of a circle (segments parameter) since
Blender meshes are polygonal.

Coordinate convention: Blender native X/Y/Z with Z=up. All
cylinders are Z-axis-aligned (the axis points up).
"""

import math

from .mesh_primitives import add_mesh_from_pydata


def build_cylindrical_shell(name, center_xz, inner_radius, outer_radius,
                            z_bottom, z_top, segments=24,
                            material=None, usage=None):
    """Build a hollow cylindrical shell (annulus extruded along Z).

    Two concentric N-gons — inner radius and outer radius — connected
    by radial quads at top and bottom to seal the ring. Result is a
    solid ring wall the player can't see through.

    Args:
      name: Blender object/node name.
      center_xz: (cx, cy) in Blender XY (the cylinder's central axis).
      inner_radius: hollow interior radius.
      outer_radius: outer wall radius.
      z_bottom, z_top: vertical extent.
      segments: number of polygon sides approximating the circle.
        24 gives a smooth read at ~30m viewing distance.
      material, usage: passed through to add_mesh_from_pydata.
    """
    cx, cy = center_xz
    verts = []
    # Vert layout: [inner_bottom_ring, inner_top_ring,
    #               outer_bottom_ring, outer_top_ring]
    # Each ring has `segments` verts.
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        # inner bottom
        verts.append((cx + inner_radius * cs, cy + inner_radius * sn, z_bottom))
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        # inner top
        verts.append((cx + inner_radius * cs, cy + inner_radius * sn, z_top))
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        # outer bottom
        verts.append((cx + outer_radius * cs, cy + outer_radius * sn, z_bottom))
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        # outer top
        verts.append((cx + outer_radius * cs, cy + outer_radius * sn, z_top))

    ib = 0                 # inner bottom ring base index
    it = segments          # inner top ring
    ob = 2 * segments      # outer bottom ring
    ot = 3 * segments      # outer top ring

    faces = []
    for i in range(segments):
        j = (i + 1) % segments
        # Inner face (visible from inside the shell — normal points INWARD,
        # so wind CW when viewed from outside)
        faces.append((ib + i, ib + j, it + j, it + i))
        # Outer face (visible from outside — normal points OUTWARD, wind CCW)
        faces.append((ob + j, ob + i, ot + i, ot + j))
        # Top annular face (visible from above, normal +Z)
        faces.append((it + i, it + j, ot + j, ot + i))
        # Bottom annular face (visible from below, normal -Z)
        faces.append((ib + j, ib + i, ob + i, ob + j))

    return add_mesh_from_pydata(
        name=name,
        verts=verts,
        faces=faces,
        material=material,
        usage=usage,
    )


def build_cylindrical_disk(name, center_xz, radius, z_bottom, z_top,
                           segments=24, material=None, usage=None):
    """Solid cylinder (disk with height). Used for columns, filled
    drum tops, plinth caps."""
    cx, cy = center_xz
    verts = []
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        verts.append((cx + radius * cs, cy + radius * sn, z_bottom))
    for i in range(segments):
        theta = (2.0 * math.pi * i) / segments
        cs, sn = math.cos(theta), math.sin(theta)
        verts.append((cx + radius * cs, cy + radius * sn, z_top))
    # Add cap centers
    verts.append((cx, cy, z_bottom))  # bottom cap center — index 2*segments
    verts.append((cx, cy, z_top))     # top cap center — index 2*segments + 1
    bc = 2 * segments
    tc = 2 * segments + 1

    faces = []
    for i in range(segments):
        j = (i + 1) % segments
        # side face
        faces.append((i, j, segments + j, segments + i))
        # bottom cap triangle (fan from center)
        faces.append((bc, j, i))
        # top cap triangle (fan from center)
        faces.append((tc, segments + i, segments + j))

    return add_mesh_from_pydata(
        name=name,
        verts=verts,
        faces=faces,
        material=material,
        usage=usage,
    )
