#!/usr/bin/env python3
"""Coplanar surface detector for the chapel mesh.

Scans every primitive's faces, identifies the axis-aligned planes
they occupy, and reports pairs of (different-primitive) coplanar
surfaces whose AABBs in the other two axes also overlap. Those
pairs WILL z-fight as the camera moves.

This is the structural defense against the recurring flicker bugs
we kept hitting. Run after gen_chapel_and_descent_export.sh.

Approach:
1. For each primitive, find its axis-aligned faces (faces whose
   triangle vertices share the same X, Y, or Z value).
2. For each axis-aligned face, record:
   - the axis it's perpendicular to (X, Y, or Z)
   - the value of that coordinate
   - the AABB of the face in the other two axes
3. Bin all faces by (axis, value-quantized-to-mm).
4. For each bin with >1 face: report every pair of different-primitive
   faces whose 2D AABBs overlap.

Exit code 0 = no coplanar pairs, 1 = pairs found.
"""

from __future__ import annotations

import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
GLB_PATH = REPO / "games/selva-oscura/assets/world/static_meshes/crypt.glb"

# Tolerance for "same plane." Faces within 1mm of each other in the
# perpendicular axis are considered coplanar.
PLANE_EPS = 0.001  # 1mm

# AABB overlap tolerance — pairs that overlap by LESS than this in
# either 2D dimension are not real overlaps (just touching at an edge).
OVERLAP_EPS = 0.001  # 1mm

# Min 2D overlap area (m^2) for a z-fight pair to be reported.
# Filters out hair-thin slivers that don't visibly flicker.
MIN_OVERLAP_AREA = 0.005  # 0.005 m^2 = ~50 cm^2

# Allowlist of (primitive_name, axis, value) tuples for surfaces we
# WANT to be coplanar (e.g. butt-jointed flush surfaces). These are
# legitimate seams, not flicker bugs. Format: prim_name_substring.
ALLOWLIST_PAIRS = [
    # The descent steps butt against each other top-of-tread to
    # bottom-of-next-step. 400 steps, all coplanar by design — skip.
    ("descent_step_", "descent_step_"),
    # Plinth flanks meet at the corner with plinth sides
    ("crypt_plinth_back_", "crypt_plinth_"),
]


def read_glb(path: Path):
    with open(path, "rb") as f:
        f.read(12)
        json_len = struct.unpack("<I", f.read(4))[0]
        f.read(4)
        js = f.read(json_len).decode("utf-8")
        bin_len = struct.unpack("<I", f.read(4))[0]
        f.read(4)
        return json.loads(js), f.read(bin_len)


def vec3_accessor(gltf, bin_blob, accessor_idx):
    a = gltf["accessors"][accessor_idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    return [struct.unpack_from("<fff", bin_blob, off + i * 12)
            for i in range(a["count"])]


def index_accessor(gltf, bin_blob, accessor_idx):
    a = gltf["accessors"][accessor_idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    ct = a["componentType"]
    fmt, sz = {5121: ("<B", 1), 5123: ("<H", 2), 5125: ("<I", 4)}[ct]
    return [struct.unpack_from(fmt, bin_blob, off + i * sz)[0]
            for i in range(a["count"])]


def axis_aligned_face(verts, eps=PLANE_EPS):
    """If all 3 verts share the same value on one axis, return
    (axis, value, normal_sign). Normal sign is +1 if face winds CCW
    when viewed from +axis direction (= face normal points +axis),
    -1 if winds CW (= face normal points -axis). Otherwise None.

    Two coplanar faces only z-fight if they have the SAME normal
    sign — i.e. they face the same direction. Opposite-normal pairs
    are butt joints (one face is hidden behind the other from each
    viewing angle)."""
    for axis in (0, 1, 2):
        v0 = verts[0][axis]
        if all(abs(v[axis] - v0) < eps for v in verts):
            # Compute the cross product's component along `axis` to
            # determine winding. CCW winding viewed from +axis = normal
            # points +axis.
            a, b, c = verts
            other = [i for i in range(3) if i != axis]
            ax = b[other[0]] - a[other[0]]
            ay = b[other[1]] - a[other[1]]
            bx = c[other[0]] - a[other[0]]
            by = c[other[1]] - a[other[1]]
            cross = ax * by - ay * bx
            normal_sign = 1 if cross > 0 else -1
            return (axis, v0, normal_sign)
    return None


def face_aabb_2d(verts, primary_axis):
    """Return the 2D AABB (a1_min, a1_max, a2_min, a2_max) for axes
    other than `primary_axis`."""
    others = [a for a in range(3) if a != primary_axis]
    a1_vals = [v[others[0]] for v in verts]
    a2_vals = [v[others[1]] for v in verts]
    return (min(a1_vals), max(a1_vals), min(a2_vals), max(a2_vals))


def aabb_overlap_2d(a, b):
    """Return overlap (a1_overlap, a2_overlap) or None if non-overlap."""
    a1o = min(a[1], b[1]) - max(a[0], b[0])
    a2o = min(a[3], b[3]) - max(a[2], b[2])
    if a1o > OVERLAP_EPS and a2o > OVERLAP_EPS:
        return (a1o, a2o)
    return None


def is_allowlisted(name_a, name_b):
    for pat_a, pat_b in ALLOWLIST_PAIRS:
        if pat_a in name_a and pat_b in name_b:
            return True
        if pat_a in name_b and pat_b in name_a:
            return True
    return False


def main():
    if not GLB_PATH.exists():
        print(f"ERROR: {GLB_PATH} not found", file=sys.stderr)
        return 2
    gltf, bin_blob = read_glb(GLB_PATH)

    # For each (axis, plane_value), collect (prim_name, 2d_aabb) entries
    AXIS_LABEL = {0: "X", 1: "Y(up)", 2: "Z(depth)"}
    planes = defaultdict(list)  # (axis, qval) -> [(name, 2d_aabb)]
    faces_processed = 0
    prims_processed = 0
    for node in gltf["nodes"]:
        name = node.get("name", "")
        if "acheron" in name.lower():
            continue
        if node.get("mesh") is None:
            continue
        mesh = gltf["meshes"][node["mesh"]]
        t = node.get("translation", [0.0, 0.0, 0.0])
        for prim in mesh["primitives"]:
            pos_acc = prim["attributes"]["POSITION"]
            positions = vec3_accessor(gltf, bin_blob, pos_acc)
            world = [(p[0] + t[0], p[1] + t[1], p[2] + t[2]) for p in positions]
            if "indices" in prim:
                indices = index_accessor(gltf, bin_blob, prim["indices"])
            else:
                indices = list(range(len(world)))
            # Collect axis-aligned faces with normal sign so we can
            # distinguish co-facing (z-fight) from anti-facing (butt joint).
            prim_faces = []
            for i in range(0, len(indices), 3):
                if i + 2 >= len(indices):
                    break
                tri = [world[indices[i]], world[indices[i + 1]], world[indices[i + 2]]]
                af = axis_aligned_face(tri)
                if af is None:
                    continue
                axis, val, normal_sign = af
                aabb2d = face_aabb_2d(tri, axis)
                prim_faces.append((axis, val, normal_sign, aabb2d))
                faces_processed += 1
            # Union triangles in the same plane + same normal-sign to a
            # single face AABB per (axis, value, sign).
            by_plane = defaultdict(lambda: None)  # (axis, qval, sign) -> 2d_aabb
            for axis, val, sign, aabb2d in prim_faces:
                qkey = (axis, round(val / PLANE_EPS) * PLANE_EPS, sign)
                cur = by_plane[qkey]
                if cur is None:
                    by_plane[qkey] = aabb2d
                else:
                    by_plane[qkey] = (
                        min(cur[0], aabb2d[0]), max(cur[1], aabb2d[1]),
                        min(cur[2], aabb2d[2]), max(cur[3], aabb2d[3]),
                    )
            for qkey, aabb2d in by_plane.items():
                planes[qkey].append((name, aabb2d))
        prims_processed += 1

    # Detect z-fight pairs: same plane + SAME NORMAL SIGN, different
    # prim names, AABBs overlap. Same-normal-sign = both surfaces face
    # the same direction (true z-fight). Opposite-sign coplanar faces
    # are butt joints and not flicker risks.
    zfight_pairs = []
    for (axis, val, sign), faces in planes.items():
        if len(faces) < 2:
            continue
        for i in range(len(faces)):
            for j in range(i + 1, len(faces)):
                name_a, aabb_a = faces[i]
                name_b, aabb_b = faces[j]
                if name_a == name_b:
                    continue
                if is_allowlisted(name_a, name_b):
                    continue
                ov = aabb_overlap_2d(aabb_a, aabb_b)
                if ov is None:
                    continue
                area = ov[0] * ov[1]
                if area < MIN_OVERLAP_AREA:
                    continue
                zfight_pairs.append((axis, val, sign, name_a, name_b, aabb_a, aabb_b, area))

    # Report
    print("=" * 76)
    print("Chapel coplanar surface audit (z-fight detector)")
    print("=" * 76)
    print(f"Primitives processed: {prims_processed}")
    print(f"Axis-aligned faces:   {faces_processed}")
    print(f"Distinct planes:      {len(planes)}")
    print(f"Z-fight pairs found:  {len(zfight_pairs)}")
    print()
    if zfight_pairs:
        zfight_pairs.sort(key=lambda x: -x[7])  # by overlap area desc
        print(f"Pairs that WILL z-fight (sorted by overlap area, largest first):")
        print()
        for axis, val, sign, name_a, name_b, aabb_a, aabb_b, area in zfight_pairs[:40]:
            ax = AXIS_LABEL[axis]
            sign_label = "+" if sign > 0 else "-"
            print(f"  PLANE {ax}={val:+.3f}m  normal {sign_label}{ax}  overlap={area*1e4:.0f} cm^2")
            print(f"    '{name_a}' 2D AABB: {aabb_a}")
            print(f"    '{name_b}' 2D AABB: {aabb_b}")
            print()
        if len(zfight_pairs) > 40:
            print(f"  ... ({len(zfight_pairs) - 40} more)")
        print()
        print("Each pair has two surfaces in the same plane with overlapping")
        print("AABBs. The GPU depth test will alternate between them per frame")
        print("as the camera moves, producing visible flicker.")
        print()
        print("Fix: introduce a small offset (z_fight_safety in JOINT) between")
        print("the two surfaces in the plane axis, OR make one surface entirely")
        print("INSIDE the other primitive (buried), OR remove one of the two.")
        return 1
    else:
        print("OK: no z-fight pairs found.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
