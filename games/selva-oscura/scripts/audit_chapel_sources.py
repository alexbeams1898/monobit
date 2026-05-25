"""Chapel geometry source-of-truth audit (v2 — corrected).

Enumerates every architectural piece in THREE independent sources:
  1. The chapel static mesh nodes (assets/world/static_meshes/crypt.glb)
     -- with per-node transforms properly applied (the loader uses
     cgltf_node_transform_world; v1 of this script forgot this and
     gave totally wrong world AABBs)
  2. The C++ BoxColliders from populateCryptColliders + populateCryptDescent
  3. The C++ CylinderColliders from populateCryptApseCylinders

For each, the world-space AABB is computed. Pairs them by spatial
overlap so we can see dual-source-of-truth divergences in one shot
and decide which side should be authoritative.

Run from repo root:
  python games/selva-oscura/scripts/audit_chapel_sources.py
"""
import json
import math
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
REPO = Path(__file__).resolve().parents[3]
GLB = REPO / "games/selva-oscura/assets/world/static_meshes/crypt.glb"

# ---------------------------------------------------------------------------
# CryptLayout.h constants (kept in sync by hand — TODO if this audit
# becomes long-lived, parse the header).
# ---------------------------------------------------------------------------
kCryptX = 0.0
kCryptZ = -210.0
kHalfWidth = 3.0
kHalfLength = 4.0
kWallThickness = 0.6
kPlinthHeight = 0.30
kDoorHalfWidth = 0.5
kWallHeight = 5.0
kDoorHeight = 2.2
kApseRadius = 1.4
kChapelGroundY = 32.25
kZFightOffset = 0.01

kSingleFlightHalfWidth = 1.50
kUpperFlightStepCount = 9
kStairRise = 0.16
kStairTread = 0.35
kUpperFlightTopChapelLocalY = 0.0
kDescentForwardSign = 1
kLandingDepthY = 2.0
kLandingWidthX = 3.0
kCorridorWidth = 2.5
kCorridorSegmentCount = 4
kCorridorSegmentRun = 12.0
kCorridorSegmentDrop = 15.2
kCorridorLandingLength = 2.0
kAcheronPlatformHalfExtent = 12.0

kUpperFlightRun = kUpperFlightStepCount * kStairTread
kUpperFlightDrop = kUpperFlightStepCount * kStairRise
kInteriorHalfWidth = kHalfWidth - kWallThickness
kInteriorHalfLength = kHalfLength - kWallThickness

CHAPEL_ORIGIN = (
    kCryptX,
    kChapelGroundY - kPlinthHeight + kZFightOffset,
    kCryptZ,
)

# ---------------------------------------------------------------------------
# .glb loader (with proper node-transform handling — replicates what the
# C++ cgltf_node_transform_world() does)
# ---------------------------------------------------------------------------

def load_glb(path):
    data = path.read_bytes()
    assert data[0:4] == b"glTF"
    total = struct.unpack("<I", data[8:12])[0]
    cursor = 12
    json_chunk = None
    bin_chunk = None
    while cursor < total:
        clen = struct.unpack("<I", data[cursor:cursor+4])[0]
        ctype = data[cursor+4:cursor+8]
        body = data[cursor+8:cursor+8+clen]
        if ctype == b"JSON":
            json_chunk = json.loads(body.decode("utf-8"))
        elif ctype == b"BIN\x00":
            bin_chunk = body
        cursor += 8 + clen
    return json_chunk, bin_chunk


def build_parent_map(gltf):
    """node_idx -> parent_idx (or None)."""
    parent = [None] * len(gltf["nodes"])
    for i, n in enumerate(gltf["nodes"]):
        for c in n.get("children", []):
            parent[c] = i
    return parent


def node_local_matrix(node):
    """Replicate glTF's TRS-to-matrix. Returns a 16-float column-major matrix.
    glTF spec: M = T * R * S, applied right-to-left on a column vector.
    If the node has a `matrix` field, that's used directly instead.
    """
    if "matrix" in node:
        return list(node["matrix"])
    # Defaults
    t = node.get("translation", [0, 0, 0])
    r = node.get("rotation", [0, 0, 0, 1])  # quat xyzw
    s = node.get("scale", [1, 1, 1])
    # Quat -> 3x3
    x, y, z, w = r
    xx, yy, zz = x*x, y*y, z*z
    xy, xz, yz = x*y, x*z, y*z
    wx, wy, wz = w*x, w*y, w*z
    R = [
        1 - 2*(yy + zz), 2*(xy + wz),     2*(xz - wy),     0,
        2*(xy - wz),     1 - 2*(xx + zz), 2*(yz + wx),     0,
        2*(xz + wy),     2*(yz - wx),     1 - 2*(xx + yy), 0,
        0, 0, 0, 1,
    ]
    # Apply scale (column scaling — column k *= s[k])
    M = list(R)
    for k in range(3):
        M[k*4 + 0] *= s[k]
        M[k*4 + 1] *= s[k]
        M[k*4 + 2] *= s[k]
    # Apply translation
    M[12] = t[0]
    M[13] = t[1]
    M[14] = t[2]
    return M


def mat4_mul(A, B):
    """Column-major 4x4 multiply: out = A * B."""
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            s = 0.0
            for k in range(4):
                s += A[k*4 + row] * B[col*4 + k]
            out[col*4 + row] = s
    return out


def node_world_matrix(gltf, node_idx, parent_map):
    """Walk up the chain accumulating world matrix."""
    chain = []
    i = node_idx
    while i is not None:
        chain.append(i)
        i = parent_map[i]
    M = [1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1]
    for i in reversed(chain):
        M = mat4_mul(M, node_local_matrix(gltf["nodes"][i]))
    return M


def transform_point(M, p):
    x = M[0]*p[0] + M[4]*p[1] + M[8]*p[2] + M[12]
    y = M[1]*p[0] + M[5]*p[1] + M[9]*p[2] + M[13]
    z = M[2]*p[0] + M[6]*p[1] + M[10]*p[2] + M[14]
    return (x, y, z)


def accessor_local_minmax(gltf, accessor_idx):
    acc = gltf["accessors"][accessor_idx]
    return tuple(acc["min"]), tuple(acc["max"])


def transformed_aabb(M, local_min, local_max):
    """Transform an axis-aligned box by a matrix, return new AABB enclosing
    all 8 transformed corners."""
    corners = []
    for ix in (0, 1):
        for iy in (0, 1):
            for iz in (0, 1):
                p = (
                    local_max[0] if ix else local_min[0],
                    local_max[1] if iy else local_min[1],
                    local_max[2] if iz else local_min[2],
                )
                corners.append(transform_point(M, p))
    mn = [min(c[i] for c in corners) for i in range(3)]
    mx = [max(c[i] for c in corners) for i in range(3)]
    return tuple(mn), tuple(mx)


def mesh_primitive_aabbs(gltf):
    """List of (name, world_min, world_max) for every mesh primitive,
    with per-node TRS chain applied, then CHAPEL_ORIGIN translation
    on top (matching what PhysicsScene::registerChapel does)."""
    parent_map = build_parent_map(gltf)
    results = []
    for ni, node in enumerate(gltf["nodes"]):
        if "mesh" not in node:
            continue
        node_world = node_world_matrix(gltf, ni, parent_map)
        mesh = gltf["meshes"][node["mesh"]]
        for prim in mesh.get("primitives", []):
            pos_idx = prim["attributes"].get("POSITION")
            if pos_idx is None:
                continue
            lmn, lmx = accessor_local_minmax(gltf, pos_idx)
            wmn, wmx = transformed_aabb(node_world, lmn, lmx)
            # Then add the chapel-world translation that the renderer
            # and PhysicsScene::registerChapel apply on top.
            wmn = tuple(wmn[i] + CHAPEL_ORIGIN[i] for i in range(3))
            wmx = tuple(wmx[i] + CHAPEL_ORIGIN[i] for i in range(3))
            results.append((node.get("name", "<unnamed>"), wmn, wmx))
    return results


# ---------------------------------------------------------------------------
# C++ collider replication
# ---------------------------------------------------------------------------

def box(name, center_xy, half_xz, y_base, half_height_y,
        walkable=False, camera_only=False, top_slope=(0, 0)):
    cx, cz = center_xy
    hx, hz = half_xz
    return {
        "name": name,
        "kind": "box",
        "world_min": (cx - hx, y_base, cz - hz),
        "world_max": (cx + hx, y_base + 2 * half_height_y, cz + hz),
        "walkable": walkable,
        "camera_only": camera_only,
        "top_slope": top_slope,
    }


def cylinder(name, center_xyz, radius, half_height, collision_only=False):
    cx, cy, cz = center_xyz
    return {
        "name": name,
        "kind": "cylinder",
        "world_min": (cx - radius, cy, cz - radius),
        "world_max": (cx + radius, cy + 2 * half_height, cz + radius),
        "collision_only": collision_only,
    }


def populate_crypt_colliders():
    out = []
    half_thick = kWallThickness * 0.5
    wall_hh = kWallHeight * 0.5
    y_base = kChapelGroundY
    front_slab_z = kCryptZ + kHalfLength - half_thick
    out.append(box("wall_front_left",
        (kCryptX + (-kHalfWidth + -kDoorHalfWidth) * 0.5, front_slab_z),
        ((-kDoorHalfWidth - -kHalfWidth) * 0.5, half_thick),
        y_base, wall_hh))
    out.append(box("wall_front_right",
        (kCryptX + (kDoorHalfWidth + kHalfWidth) * 0.5, front_slab_z),
        ((kHalfWidth - kDoorHalfWidth) * 0.5, half_thick),
        y_base, wall_hh))
    back_y = kCryptZ - kHalfLength + half_thick
    out.append(box("wall_back_left",
        (kCryptX + (-kHalfWidth + -kSingleFlightHalfWidth) * 0.5, back_y),
        ((-kSingleFlightHalfWidth - -kHalfWidth) * 0.5, half_thick),
        y_base, wall_hh))
    out.append(box("wall_back_right",
        (kCryptX + (kSingleFlightHalfWidth + kHalfWidth) * 0.5, back_y),
        ((kHalfWidth - kSingleFlightHalfWidth) * 0.5, half_thick),
        y_base, wall_hh))
    out.append(box("wall_side_left",
        (kCryptX - kHalfWidth + half_thick, kCryptZ),
        (half_thick, kHalfLength),
        y_base, wall_hh))
    out.append(box("wall_side_right",
        (kCryptX + kHalfWidth - half_thick, kCryptZ),
        (half_thick, kHalfLength),
        y_base, wall_hh))
    out.append(box("door_header",
        (kCryptX, front_slab_z),
        (kDoorHalfWidth, half_thick),
        y_base + kDoorHeight, (kWallHeight - kDoorHeight) * 0.5,
        camera_only=True))
    out.append(box("roof",
        (kCryptX, kCryptZ - kApseRadius * 0.5),
        (kHalfWidth, kHalfLength + kApseRadius * 0.5),
        y_base + kWallHeight, 0.5,
        camera_only=True))
    return out


def populate_crypt_descent():
    out = []
    ground_y = kChapelGroundY

    def push(name, cx, cy, cz, hx, hy, hz, walkable, camera_only, top_slope=(0, 0)):
        center = (kCryptX + cx, kCryptZ - cy)
        half_xz = (hx, hy)
        y_base = (ground_y - kPlinthHeight + kZFightOffset) + cz - 2.0 * hz
        out.append(box(name, center, half_xz, y_base, hz,
                       walkable=walkable, camera_only=camera_only,
                       top_slope=top_slope))

    floor_slab = 0.10
    push("chapel_floor_front", 0.0, -kInteriorHalfLength * 0.5, kPlinthHeight,
         kInteriorHalfWidth, kInteriorHalfLength * 0.5,
         floor_slab * 0.5, True, False)

    side_strip_half_w = (kInteriorHalfWidth - kSingleFlightHalfWidth) * 0.5
    side_strip_cx_mag = kSingleFlightHalfWidth + side_strip_half_w
    side_strip_cy = kDescentForwardSign * (kUpperFlightRun * 0.5)
    for sign in (-1.0, 1.0):
        push("chapel_floor_side", sign * side_strip_cx_mag, side_strip_cy,
             kPlinthHeight, side_strip_half_w, kUpperFlightRun * 0.5,
             floor_slab * 0.5, True, False)

    apse_strip_cy = kDescentForwardSign * (
        kUpperFlightRun + (kInteriorHalfLength - kUpperFlightRun) * 0.5)
    apse_strip_half_len_y = (kInteriorHalfLength - kUpperFlightRun) * 0.5
    apse_strip_half_w = (kInteriorHalfWidth - kSingleFlightHalfWidth) * 0.5
    apse_strip_cx_mag = kSingleFlightHalfWidth + apse_strip_half_w
    if apse_strip_half_len_y > 0.01 and apse_strip_half_w > 0.01:
        for sign in (-1.0, 1.0):
            push("chapel_floor_apse", sign * apse_strip_cx_mag, apse_strip_cy,
                 kPlinthHeight, apse_strip_half_w, apse_strip_half_len_y,
                 floor_slab * 0.5, True, False)

    stair_thick = 0.5
    ramp_top_cz_center = (kPlinthHeight - kStairRise) - kUpperFlightDrop * 0.5
    ramp_cy_center = kUpperFlightTopChapelLocalY + kDescentForwardSign * kUpperFlightRun * 0.5
    ramp_slope = (kStairRise / kStairTread) * kDescentForwardSign
    push("upper_flight_ramp", 0.0, ramp_cy_center, ramp_top_cz_center,
         kSingleFlightHalfWidth, kUpperFlightRun * 0.5,
         stair_thick * 0.5, True, False, (0.0, ramp_slope))

    landing_top_z = kPlinthHeight - kUpperFlightDrop
    landing_overlap = 0.5 * kStairTread
    le_near = kUpperFlightTopChapelLocalY + kDescentForwardSign * (
        kUpperFlightRun - landing_overlap)
    le_far = kUpperFlightTopChapelLocalY + kDescentForwardSign * (
        kUpperFlightRun + kLandingDepthY)
    landing_cy = (le_near + le_far) * 0.5
    landing_half_len = abs(le_far - le_near) * 0.5
    push("landing_top", 0.0, landing_cy, landing_top_z,
         kLandingWidthX * 0.5, landing_half_len,
         0.4 * 0.5, True, False)

    cursor_cy = kUpperFlightTopChapelLocalY + kDescentForwardSign * (
        kUpperFlightRun + kLandingDepthY)
    cursor_cz = landing_top_z
    corridor_hw = kCorridorWidth * 0.5
    corridor_slope = (kCorridorSegmentDrop / kCorridorSegmentRun) * kDescentForwardSign
    for seg in range(kCorridorSegmentCount):
        ramp_cy = cursor_cy + kDescentForwardSign * kCorridorSegmentRun * 0.5
        ramp_top = cursor_cz - kCorridorSegmentDrop * 0.5
        push(f"corridor_ramp_{seg}", 0.0, ramp_cy, ramp_top,
             corridor_hw, kCorridorSegmentRun * 0.5,
             stair_thick * 0.5, True, False, (0.0, corridor_slope))
        cursor_cy += kDescentForwardSign * kCorridorSegmentRun
        cursor_cz -= kCorridorSegmentDrop
        if seg < kCorridorSegmentCount - 1:
            lrun = kCorridorLandingLength
            lcy = cursor_cy + kDescentForwardSign * lrun * 0.5
            push(f"corridor_landing_{seg}", 0.0, lcy, cursor_cz,
                 corridor_hw, lrun * 0.5,
                 stair_thick * 0.5, True, False)
            cursor_cy += kDescentForwardSign * lrun

    acheron_top_z = cursor_cz
    acheron_cy = cursor_cy + kDescentForwardSign * kAcheronPlatformHalfExtent
    push("acheron_platform", 0.0, acheron_cy, acheron_top_z,
         kAcheronPlatformHalfExtent, kAcheronPlatformHalfExtent,
         stair_thick * 0.5, True, False)
    return out


# ---------------------------------------------------------------------------
# Pairing
# ---------------------------------------------------------------------------

def aabb_overlap_xz(a_min, a_max, b_min, b_max, tol=0.1):
    return (a_min[0] - tol <= b_max[0] and b_min[0] <= a_max[0] + tol and
            a_min[2] - tol <= b_max[2] and b_min[2] <= a_max[2] + tol)


def aabb_overlap_3d(a_min, a_max, b_min, b_max, tol=0.1):
    return all(a_min[i] - tol <= b_max[i] and b_min[i] <= a_max[i] + tol for i in range(3))


def aabb_size(mn, mx):
    return tuple(mx[i] - mn[i] for i in range(3))


def fmt_aabb(mn, mx):
    sz = aabb_size(mn, mx)
    return (f"x[{mn[0]:7.2f},{mx[0]:7.2f}] "
            f"y[{mn[1]:7.2f},{mx[1]:7.2f}] "
            f"z[{mn[2]:8.2f},{mx[2]:8.2f}]  "
            f"size {sz[0]:5.1f}x{sz[1]:5.1f}x{sz[2]:5.1f}")


def main():
    print("=== Chapel source-of-truth audit (v2) ===\n")
    print(f"Chapel world origin (added by both renderer + PhysicsScene): {CHAPEL_ORIGIN}\n")

    if not GLB.exists():
        print(f"ERROR: {GLB} not found")
        sys.exit(1)
    gltf, _ = load_glb(GLB)
    mesh_prims = mesh_primitive_aabbs(gltf)

    print(f"=== Mesh primitives ({len(mesh_prims)}) ===")
    for name, mn, mx in sorted(mesh_prims, key=lambda r: (r[1][2], r[0])):
        print(f"  {name:35s}  {fmt_aabb(mn, mx)}")

    cpp = populate_crypt_colliders() + populate_crypt_descent()
    print(f"\n=== C++ BoxColliders ({len(cpp)}) ===")
    for c in sorted(cpp, key=lambda r: r['world_min'][2]):
        flags = []
        if c.get("walkable"): flags.append("WALK")
        if c.get("camera_only"): flags.append("CAM")
        if abs(c["top_slope"][0]) > 1e-5 or abs(c["top_slope"][1]) > 1e-5:
            flags.append(f"SLOPE{c['top_slope']}")
        fl = " ".join(flags) if flags else "-"
        print(f"  {c['name']:35s}  {fmt_aabb(c['world_min'], c['world_max'])}  [{fl}]")

    # --- Categorize each mesh prim ---
    print("\n=== Per-mesh-prim divergence vs C++ colliders ===")
    print("(EXACT = AABBs match within 5cm; SHIFTED = overlapping XZ but Y or extent differs;")
    print(" SOLO = no overlapping C++ collider)\n")
    mesh_only = []
    matched_exact = []
    matched_shifted = []
    for name, mn, mx in mesh_prims:
        # Look for a C++ collider whose AABB is roughly the same
        exact = None
        any_overlap = []
        for c in cpp:
            same = all(abs(mn[i] - c["world_min"][i]) < 0.10 and
                       abs(mx[i] - c["world_max"][i]) < 0.10 for i in range(3))
            if same:
                exact = c
            if aabb_overlap_3d(mn, mx, c["world_min"], c["world_max"]):
                any_overlap.append(c)
        if exact is not None:
            matched_exact.append((name, exact["name"]))
        elif any_overlap:
            matched_shifted.append((name, [c["name"] for c in any_overlap]))
        else:
            mesh_only.append(name)

    if matched_exact:
        print("  EXACT-MATCH pairs (duplicate data; pick one source, delete other):")
        for m, c in matched_exact:
            print(f"    mesh '{m}'  ==  C++ '{c}'")

    if matched_shifted:
        print("\n  SHIFTED pairs (overlapping but not identical — DRIFT, needs reconciliation):")
        for m, cs in matched_shifted:
            print(f"    mesh '{m}'  overlaps  C++ {cs}")

    if mesh_only:
        print(f"\n  MESH-ONLY ({len(mesh_only)}): visual geometry with no C++ collider counterpart")
        for n in mesh_only:
            print(f"    {n}")

    # --- C++ colliders with no mesh counterpart ---
    print("\n=== C++-only colliders (collision without visible mesh) ===")
    code_only = []
    for c in cpp:
        if not any(aabb_overlap_3d(mn, mx, c["world_min"], c["world_max"])
                   for _, mn, mx in mesh_prims):
            code_only.append(c["name"])
    if code_only:
        for n in code_only:
            print(f"  {n}")
    else:
        print("  (none — every C++ collider has at least one overlapping mesh primitive)")

    # --- Where is the player getting stuck? ---
    print("\n=== Spatial query: what's near the player's stuck position? ===")
    # Player got stuck at (-0.237, 31.961, -213.630) facing into the descent.
    # Capsule extends y=31.96 to y=33.76, radius 0.35.
    PX, PY_FOOT, PY_TOP, PZ, R = -0.237, 31.961, 33.761, -213.630, 0.35
    print(f"Player capsule: x[{PX-R:.2f},{PX+R:.2f}] y[{PY_FOOT:.2f},{PY_TOP:.2f}] z[{PZ-R:.2f},{PZ+R:.2f}]\n")
    print("Mesh primitives within 1m of capsule:")
    found_any = False
    for name, mn, mx in mesh_prims:
        if (mn[0] <= PX + R + 1.0 and PX - R - 1.0 <= mx[0] and
            mn[2] <= PZ + R + 1.0 and PZ - R - 1.0 <= mx[2] and
            mn[1] <= PY_TOP + 1.0 and PY_FOOT - 1.0 <= mx[1]):
            found_any = True
            print(f"  {name:35s}  {fmt_aabb(mn, mx)}")
    if not found_any:
        print("  (none)")
    print("\nC++ colliders within 1m of capsule:")
    found_any = False
    for c in cpp:
        mn, mx = c["world_min"], c["world_max"]
        if (mn[0] <= PX + R + 1.0 and PX - R - 1.0 <= mx[0] and
            mn[2] <= PZ + R + 1.0 and PZ - R - 1.0 <= mx[2] and
            mn[1] <= PY_TOP + 1.0 and PY_FOOT - 1.0 <= mx[1]):
            found_any = True
            print(f"  {c['name']:35s}  {fmt_aabb(mn, mx)}")
    if not found_any:
        print("  (none)")

    # --- Sloped-box trimesh problem ---
    print("\n=== Sloped-box trimesh dimensions (Jolt registers as 12-tri mesh) ===")
    print("(check if any sloped box's bottom face is BELOW the chapel floor")
    print(" -- if so the player can fall through gaps between the sloped")
    print(" floor face and the ramp's bottom)")
    for c in cpp:
        if abs(c["top_slope"][0]) < 1e-5 and abs(c["top_slope"][1]) < 1e-5:
            continue
        mn = c["world_min"]
        mx = c["world_max"]
        print(f"  {c['name']:35s}  y[{mn[1]:.2f}..{mx[1]:.2f}]  slope{c['top_slope']}")

    print(f"\n=== Summary ===")
    print(f"  Mesh primitives:       {len(mesh_prims)}")
    print(f"  C++ box colliders:     {len(cpp)}")
    print(f"  EXACT-match pairs:     {len(matched_exact)}  (true duplicates -- pick one source)")
    print(f"  SHIFTED pairs:         {len(matched_shifted)}  (drift -- reconcile)")
    print(f"  Mesh-only primitives:  {len(mesh_only)}  (visual without collider)")
    print(f"  Code-only colliders:   {len(code_only)}  (collision without visual)")


if __name__ == "__main__":
    main()
