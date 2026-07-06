#!/usr/bin/env python3
"""Crack detector for the chapel + descent mesh.

Walks every triangle in crypt.glb. For each triangle edge, counts how
many triangles share that edge (matched within EPS tolerance). Edges
shared by exactly 2 triangles are interior seams (a manifold mesh).
Edges shared by 1 triangle are BOUNDARY EDGES — either:
  (a) a designed opening (front door, back-wall tunnel, oculus, etc),
  (b) a CRACK that lets light through where the geometry should seal.

We allowlist designed openings by world-XYZ AABB. Any boundary edge
falling outside the allowlist is reported as a crack with its world
position and the source primitive name.

Run AFTER gen_chapel_and_descent_export.sh produces crypt.glb. Outputs to stdout.
Exit code 0 = no unexpected cracks, 1 = cracks found.
"""

from __future__ import annotations

import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
GLB_PATH = REPO / "games/selva-oscura/assets/world/static_meshes/crypt.glb"

# Two edges are "the same" if their endpoints match within EPS in
# all three axes. Authored geometry uses ~5mm insets; we pick EPS
# tight enough to NOT collapse those (so insets remain visible) but
# loose enough that exact floating-point identity isn't required.
EPS = 0.001  # 1mm

# Designed openings — list of world-XYZ AABBs. Boundary edges INSIDE
# any of these AABBs are NOT reported as cracks. These describe where
# the chapel is supposed to be open to the outside.
#
# All values are CHAPEL-LOCAL because the .glb is exported in
# chapel-local space (the engine applies the chapel world origin at
# load time). Format: (xmin, ymin, zmin, xmax, ymax, zmax) where
# axes are: X=side-to-side, Y=height (up), Z=depth (toward apse is +Z
# in Blender, -Z in glTF — we use Blender convention here since this
# script reads glTF axes as raw).
#
# Note: glTF axes after Blender export are X=side, Y=up, Z=-depth.
# We match the audit_chapel_sources.py convention.
ALLOWLIST = [
    # Front door (X-span = ±DOOR_WIDTH/2, Y from chapel floor to door
    # header, Z at front-wall plane). DOOR_WIDTH=1.0, header height
    # PLINTH_HEIGHT+DOOR_HEIGHT=0.30+2.15=2.45.
    {
        "name": "front_door",
        "aabb": (-0.51, 0.29, 3.39, 0.51, 2.46, 4.06),
    },
    # Back-wall tunnel opening (X-span = ±corridor_opening_half_x = 1.85,
    # Y from chapel floor up to apse_doorway_top_z = 2.45, Z at back-wall
    # plane).
    {
        "name": "back_wall_tunnel",
        "aabb": (-1.86, 0.29, -4.01, 1.86, 2.46, -3.39),
    },
    # Oculus on the facade (circle, but we approximate with a square AABB)
    {
        "name": "oculus",
        "aabb": (-0.40, 4.62, 3.99, 0.40, 5.42, 4.04),
    },
    # The descent shaft mouth in the chapel floor (X-span and Y-span of
    # the upper-flight footprint). Y range: 0..PLINTH_HEIGHT = 0.30 (the
    # plinth slab boundary). Wait — actually the descent shaft is OPEN
    # at Y=PLINTH_HEIGHT (chapel floor). Triangles around the descent
    # opening in the chapel-floor plinth boundary are LEGIT openings.
    {
        "name": "descent_shaft_floor_opening",
        # X=±1.50 (SINGLE_FLIGHT_HALF_WIDTH), Y=0..PLINTH_HEIGHT,
        # Z=UPPER_FLIGHT_TOP_Y .. UPPER_FLIGHT_TOP_Y+upper_flight_run
        # = 0 .. 3.15 (Blender Y axis); glTF z = 0 .. -3.15
        "aabb": (-1.51, -0.01, -3.16, 1.51, 0.31, 0.01),
    },
    # The continuous descent tunnel extends from chapel-local Z (Blender Y)
    # = +3.15 (upper flight bottom) down 140m. The corridor walls + steps
    # are inside this volume. Boundary edges along the corridor's chapel-
    # facing end and inside the corridor are legit (player walks through).
    # We DON'T allowlist the whole 140m corridor — only the chapel-side
    # opening where it meets the back-wall tunnel.
    {
        "name": "descent_corridor_chapel_end",
        # X=±corridor_outer_half = ±1.80, Y=-3..+3 (height range),
        # glTF Z = -3.5..-3.0 (covers chapel-side end of corridor)
        "aabb": (-1.81, -3.50, -3.50, 1.81, 3.00, -3.00),
    },
]


def read_glb(path: Path):
    """Read .glb binary chunks. Returns (json_dict, bin_blob)."""
    with open(path, "rb") as f:
        magic, version, total_len = struct.unpack("<III", f.read(12))
        assert magic == 0x46546C67, "not a .glb"
        json_len = struct.unpack("<I", f.read(4))[0]
        f.read(4)  # chunk type 'JSON'
        js = f.read(json_len).decode("utf-8")
        bin_len = struct.unpack("<I", f.read(4))[0]
        f.read(4)  # chunk type 'BIN '
        return json.loads(js), f.read(bin_len)


def vec3_accessor(gltf, bin_blob, accessor_idx):
    a = gltf["accessors"][accessor_idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    count = a["count"]
    out = []
    for i in range(count):
        x, y, z = struct.unpack_from("<fff", bin_blob, off + i * 12)
        out.append((x, y, z))
    return out


def index_accessor(gltf, bin_blob, accessor_idx):
    a = gltf["accessors"][accessor_idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    count = a["count"]
    # Component type codes: 5121=u8, 5123=u16, 5125=u32
    ct = a["componentType"]
    fmt, sz = {5121: ("<B", 1), 5123: ("<H", 2), 5125: ("<I", 4)}[ct]
    out = []
    for i in range(count):
        (idx,) = struct.unpack_from(fmt, bin_blob, off + i * sz)
        out.append(idx)
    return out


def quantize_point(p, scale=int(1.0 / EPS)):
    """Snap a point to an EPS grid so float-equal lookups work."""
    return (round(p[0] * scale), round(p[1] * scale), round(p[2] * scale))


def canonical_edge(a, b):
    """Sort endpoints so edge (A,B) and (B,A) hash the same."""
    qa = quantize_point(a)
    qb = quantize_point(b)
    return (qa, qb) if qa < qb else (qb, qa)


def edge_midpoint(a, b):
    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (a[2] + b[2]) * 0.5)


def point_in_aabb(p, aabb):
    xmin, ymin, zmin, xmax, ymax, zmax = aabb
    return (xmin <= p[0] <= xmax and ymin <= p[1] <= ymax and zmin <= p[2] <= zmax)


def main():
    if not GLB_PATH.exists():
        print(f"ERROR: {GLB_PATH} not found", file=sys.stderr)
        return 2
    gltf, bin_blob = read_glb(GLB_PATH)

    # Walk every chapel/descent primitive's triangles. Acheron is a
    # separate landmass — skip. Descent steps are 400 individual boxes,
    # all internally manifold; we INCLUDE them for completeness.
    edges = defaultdict(list)  # canonical_edge → [(prim_name, midpoint_world), ...]

    nodes_processed = 0
    tris_processed = 0
    for node in gltf["nodes"]:
        name = node.get("name", "")
        if "acheron" in name.lower():
            continue
        if node.get("mesh") is None:
            continue
        mesh = gltf["meshes"][node["mesh"]]
        translation = node.get("translation", [0.0, 0.0, 0.0])
        for prim in mesh["primitives"]:
            pos_acc = prim["attributes"]["POSITION"]
            positions = vec3_accessor(gltf, bin_blob, pos_acc)
            # World position = node translation + vert
            world_positions = [
                (p[0] + translation[0], p[1] + translation[1], p[2] + translation[2])
                for p in positions
            ]
            if "indices" in prim:
                indices = index_accessor(gltf, bin_blob, prim["indices"])
            else:
                # Implicit indexing: 0,1,2, 3,4,5, ...
                indices = list(range(len(world_positions)))
            for i in range(0, len(indices), 3):
                if i + 2 >= len(indices):
                    break
                a = world_positions[indices[i]]
                b = world_positions[indices[i + 1]]
                c = world_positions[indices[i + 2]]
                tris_processed += 1
                for e in (canonical_edge(a, b), canonical_edge(b, c), canonical_edge(c, a)):
                    edges[e].append((name, edge_midpoint(
                        positions[indices[i]] if e == canonical_edge(a, b) else
                        positions[indices[i + 1]] if e == canonical_edge(b, c) else
                        positions[indices[i + 2]], a)))
        nodes_processed += 1

    # Boundary edges are those shared by an ODD number of triangles
    # (manifold edges are shared by 2; cracks are shared by 1).
    # Edges shared by 3+ are non-manifold but rare in axis-aligned
    # architecture; we report them as well.
    boundary = []
    nonmanifold = []
    interior = 0
    for e, owners in edges.items():
        if len(owners) == 1:
            boundary.append((e, owners))
        elif len(owners) == 2:
            interior += 1
        else:
            nonmanifold.append((e, owners))

    # Convert grid-quantized endpoints back to world coords for reporting
    def unquantize(qp):
        return (qp[0] * EPS, qp[1] * EPS, qp[2] * EPS)

    # Categorize boundary edges: allowlisted vs. cracks
    cracks = []
    allowlisted = defaultdict(int)
    for e, owners in boundary:
        (qa, qb) = e
        pa = unquantize(qa)
        pb = unquantize(qb)
        mid = ((pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5, (pa[2] + pb[2]) * 0.5)
        matched = None
        for entry in ALLOWLIST:
            if point_in_aabb(mid, entry["aabb"]):
                matched = entry["name"]
                break
        if matched:
            allowlisted[matched] += 1
        else:
            cracks.append((e, owners, pa, pb, mid))

    # Group cracks by source primitive for easier diagnosis
    cracks_by_prim = defaultdict(list)
    for e, owners, pa, pb, mid in cracks:
        names = sorted(set(o[0] for o in owners))
        for n in names:
            cracks_by_prim[n].append((pa, pb, mid))

    # ---- Report ----
    print("=" * 76)
    print("Chapel crack audit")
    print("=" * 76)
    print(f"Nodes processed:      {nodes_processed}")
    print(f"Triangles processed:  {tris_processed}")
    print(f"Unique edges:         {len(edges)}")
    print(f"Manifold (2-shared):  {interior}")
    print(f"Boundary (1-shared):  {len(boundary)}")
    print(f"Non-manifold (>2):    {len(nonmanifold)}")
    print()
    print("Allowlisted boundary edges (designed openings):")
    for name in sorted(allowlisted):
        print(f"  {allowlisted[name]:5d}  {name}")
    print()
    if cracks:
        print(f"CRACKS DETECTED: {len(cracks)} boundary edges not in any allowlist.")
        print()
        print(f"By primitive:")
        for prim in sorted(cracks_by_prim):
            entries = cracks_by_prim[prim]
            print(f"  [{len(entries):5d}]  {prim}")
            # Print sample crack positions for the worst offenders
            for pa, pb, mid in entries[:3]:
                print(f"      edge at world ({mid[0]:+.3f}, {mid[1]:+.3f}, {mid[2]:+.3f}) "
                      f"len={((pb[0]-pa[0])**2 + (pb[1]-pa[1])**2 + (pb[2]-pa[2])**2)**0.5:.3f}m")
            if len(entries) > 3:
                print(f"      ... ({len(entries) - 3} more)")
        print()
        print("Each crack is a triangle edge that's not matched by another triangle.")
        print("It corresponds to an exposed silhouette edge — a place where the")
        print("chapel mesh has an OPEN seam (light/sky visible through). To fix:")
        print("  1. Identify which primitive pair SHOULD meet at the crack midpoint")
        print("  2. Either extend one of them to butt the other, or add a")
        print("     bridging slab that closes the seam")
        return 1
    else:
        print("OK: no cracks detected (every boundary edge is in an allowlist)")
        return 0


if __name__ == "__main__":
    sys.exit(main())
