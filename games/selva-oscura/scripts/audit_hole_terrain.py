#!/usr/bin/env python3
"""Audit: at the chapel-floor stair hole, what is the actual world Y
of every terrain mesh vertex AND every interpolated point inside the
hole footprint? Tells us whether terrain pokes above plinth top.

Replicates Terrain.cpp's mesh-build + sampleHeight exactly:
  - Mesh vertices laid out on a (subdivide+1) x (subdivide+1) grid
    across world_extent, sampled via bilinearSample of the PNG
  - Quad split: tz < tx => triangle A, else triangle B (SW-NE diagonal)
  - sampleHeight = barycentric within whichever triangle the point hits
"""
import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("[audit] requires Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

REPO = Path(__file__).resolve().parents[3]
PNG = REPO / "games/selva-oscura/assets/world/terrain/selva_inner.png"

# Terrain region config (from config.json) - keep in sync.
WORLD_ORIGIN = (0.0, -180.0)
WORLD_EXTENT = 512.0
SUBDIVIDE = 384
# Height range: 0..255 px maps to HEIGHT_MIN..HEIGHT_MAX meters.
# MUST match assets/world/terrain/config.json (height_range_min,
# height_range_max). Earlier this only used HEIGHT_MAX with no min,
# so every sampled value was 0.82m off — masking the real plateau
# height (32 not 33) and giving wrong "no floating gap" conclusions.
HEIGHT_MIN = -6.0
HEIGHT_MAX = 38.0

# Chapel constants (from CryptLayout.h + gen_crypt_foundation.py).
CRYPT_X = 0.0
CRYPT_Z = -210.0
PLINTH_HEIGHT = 0.30
HALF_WIDTH = 3.0
WALL_THICKNESS = 0.6
SINGLE_FLIGHT_HALF_WIDTH = 2.40
STEP_COUNT = 9
STAIR_TREAD = 0.35
# Chapel ground anchor sample (CryptLayout.h)
CHAPEL_GROUND_SAMPLE_X = CRYPT_X + HALF_WIDTH + 2.0
CHAPEL_GROUND_SAMPLE_Z = CRYPT_Z

# Hole world extent (from build_plinth / cut_stair_holes).
HOLE_X_MIN = -SINGLE_FLIGHT_HALF_WIDTH
HOLE_X_MAX = +SINGLE_FLIGHT_HALF_WIDTH
HOLE_HALF_LEN = STEP_COUNT * STAIR_TREAD * 0.5
HOLE_CENTER_Y_LOCAL = HOLE_HALF_LEN  # chapel-local Y
# chapel-local +Y maps to world -Z
HOLE_Z_MIN = CRYPT_Z - 2 * HOLE_HALF_LEN  # furthest from spawn (apse side)
HOLE_Z_MAX = CRYPT_Z  # nearest to spawn (door side)


def bilinear_sample(img_pixels, w, h, u, v):
    """Mirror Terrain.cpp bilinearSample exactly."""
    if not img_pixels or w <= 0 or h <= 0:
        return 0.0
    fx = u * (w - 1)
    fy = v * (h - 1)
    ix = max(0, min(w - 2, int(fx)))
    iy = max(0, min(h - 2, int(fy)))
    tx = fx - ix
    ty = fy - iy
    # PNG row 0 = top (Z most-negative? engine convention TBD; mirror C++).
    p00 = img_pixels[iy * w + ix]
    p10 = img_pixels[iy * w + (ix + 1)]
    p01 = img_pixels[(iy + 1) * w + ix]
    p11 = img_pixels[(iy + 1) * w + (ix + 1)]
    h0 = p00 * (1 - tx) + p10 * tx
    h1 = p01 * (1 - tx) + p11 * tx
    t = (h0 * (1 - ty) + h1 * ty) / 255.0
    return HEIGHT_MIN + t * (HEIGHT_MAX - HEIGHT_MIN)


def build_mesh_ys(img_pixels, w, h):
    """Build the verts_per_side x verts_per_side mesh_y grid like
    buildRegionMesh does."""
    verts_per_side = SUBDIVIDE + 1
    mesh_y = [[0.0] * verts_per_side for _ in range(verts_per_side)]
    half = WORLD_EXTENT * 0.5
    for iz in range(verts_per_side):
        for ix in range(verts_per_side):
            u = ix / SUBDIVIDE
            v = iz / SUBDIVIDE
            wx = WORLD_ORIGIN[0] + (u * 2 - 1) * half
            wz = WORLD_ORIGIN[1] + (v * 2 - 1) * half
            wy = bilinear_sample(img_pixels, w, h, u, v)
            mesh_y[iz][ix] = wy
    return mesh_y, verts_per_side


def sample_height_via_mesh(mesh_y, world_x, world_z):
    """Replicate sampleHeight in Terrain.cpp exactly."""
    half = WORLD_EXTENT * 0.5
    u = (world_x - WORLD_ORIGIN[0] + half) / WORLD_EXTENT
    v = (world_z - WORLD_ORIGIN[1] + half) / WORLD_EXTENT
    if u < 0 or u > 1 or v < 0 or v > 1:
        return 0.0
    fx = u * SUBDIVIDE
    fz = v * SUBDIVIDE
    ix = max(0, min(SUBDIVIDE - 1, int(fx)))
    iz = max(0, min(SUBDIVIDE - 1, int(fz)))
    tx = fx - ix
    tz = fz - iz
    y00 = mesh_y[iz][ix]
    y10 = mesh_y[iz][ix + 1]
    y01 = mesh_y[iz + 1][ix]
    y11 = mesh_y[iz + 1][ix + 1]
    if tz < tx:
        return (1 - tx) * y00 + tz * y11 + (tx - tz) * y10
    return (1 - tz) * y00 + (tz - tx) * y01 + tx * y11


def mesh_vertex_world_xz(ix, iz):
    half = WORLD_EXTENT * 0.5
    u = ix / SUBDIVIDE
    v = iz / SUBDIVIDE
    wx = WORLD_ORIGIN[0] + (u * 2 - 1) * half
    wz = WORLD_ORIGIN[1] + (v * 2 - 1) * half
    return wx, wz


def main():
    if not PNG.exists():
        print(f"[audit] heightmap not found: {PNG}", file=sys.stderr)
        sys.exit(1)
    img = Image.open(PNG).convert("L")
    w, h = img.size
    img_pixels = list(img.getdata())
    print(f"[audit] heightmap {PNG.name}: {w}x{h}, height_max={HEIGHT_MAX}")
    print(f"[audit] mesh: subdivide={SUBDIVIDE}, extent={WORLD_EXTENT}, "
          f"step={WORLD_EXTENT/SUBDIVIDE:.4f}m per vertex")
    print(f"[audit] hole: X[{HOLE_X_MIN}, {HOLE_X_MAX}], "
          f"Z[{HOLE_Z_MIN:.3f}, {HOLE_Z_MAX}]")

    mesh_y, vps = build_mesh_ys(img_pixels, w, h)

    # 0) Terrain Y immediately around the chapel exterior (at the
    #    wall footprint and 1m past it on each side).
    print("\n[audit] terrain Y at chapel exterior boundary:")
    for label, wx, wz in [
        ("front-center (at wall)", 0.0, -206.0),
        ("front-center (1m out)", 0.0, -205.0),
        ("back-center (at wall)", 0.0, -214.0),
        ("back-center (1m out)", 0.0, -215.0),
        ("left-mid (at wall)", -3.0, -210.0),
        ("left-mid (1m out)", -4.0, -210.0),
        ("right-mid (at wall)", 3.0, -210.0),
        ("right-mid (1m out)", 4.0, -210.0),
        ("front-left corner (at wall)", -3.0, -206.0),
        ("front-right corner (at wall)", 3.0, -206.0),
        ("back-left corner (at wall)", -3.0, -214.0),
        ("back-right corner (at wall)", 3.0, -214.0),
    ]:
        y = sample_height_via_mesh(mesh_y, wx, wz)
        print(f"  {label:36s} ({wx:+.1f}, {wz:+.1f})  y={y:.3f}")
    avg_at_wall = sum(
        sample_height_via_mesh(mesh_y, wx, wz)
        for (wx, wz) in [(0, -206), (0, -214), (-3, -210), (3, -210),
                         (-3, -206), (3, -206), (-3, -214), (3, -214)]
    ) / 8.0
    print(f"  AVG terrain Y at chapel walls = {avg_at_wall:.4f}")

    # 1) Chapel anchor.
    anchor_y = sample_height_via_mesh(mesh_y, CHAPEL_GROUND_SAMPLE_X, CHAPEL_GROUND_SAMPLE_Z)
    plinth_top = anchor_y + 0.01  # kZFightOffset; plinth top = ground + offset
    print(f"\n[audit] chapel anchor sampleHeight({CHAPEL_GROUND_SAMPLE_X}, "
          f"{CHAPEL_GROUND_SAMPLE_Z}) = {anchor_y:.4f}m")
    print(f"[audit] plinth top world Y = {plinth_top:.4f}m")

    # 2) Mesh vertices inside or near the hole.
    print("\n[audit] mesh vertices in hole footprint (X in hole, Z in hole):")
    print("   ix    iz       wx       wz       wy   poke?")
    pad = 4  # also list a couple vertices past the hole for context
    half = WORLD_EXTENT * 0.5
    step = WORLD_EXTENT / SUBDIVIDE
    ix_lo = int((HOLE_X_MIN - WORLD_ORIGIN[0] + half) / step) - pad
    ix_hi = int((HOLE_X_MAX - WORLD_ORIGIN[0] + half) / step) + pad
    iz_lo = int((HOLE_Z_MIN - WORLD_ORIGIN[1] + half) / step) - pad
    iz_hi = int((HOLE_Z_MAX - WORLD_ORIGIN[1] + half) / step) + pad
    for iz in range(max(0, iz_lo), min(vps, iz_hi + 1)):
        for ix in range(max(0, ix_lo), min(vps, ix_hi + 1)):
            wx, wz = mesh_vertex_world_xz(ix, iz)
            wy = mesh_y[iz][ix]
            in_hole_x = HOLE_X_MIN - 0.5 < wx < HOLE_X_MAX + 0.5
            in_hole_z = HOLE_Z_MIN - 0.5 < wz < HOLE_Z_MAX + 0.5
            if not (in_hole_x and in_hole_z):
                continue
            inside = HOLE_X_MIN <= wx <= HOLE_X_MAX and HOLE_Z_MIN <= wz <= HOLE_Z_MAX
            poke = "*** POKE" if (inside and wy > plinth_top) else ""
            tag = "(IN)" if inside else "(out)"
            print(f"  {ix:3d}  {iz:3d}   {wx:+7.3f}  {wz:+7.3f}  "
                  f"{wy:7.3f}  {tag} {poke}")

    # 3) Dense sample grid inside the hole: 0.1m spacing.
    print("\n[audit] dense sample grid inside hole (interpolated terrain Y):")
    print(f"  threshold for poke = plinth_top {plinth_top:.4f}")
    max_y = -1e9
    max_xz = (0, 0)
    pokes = []
    nx = int((HOLE_X_MAX - HOLE_X_MIN) / 0.1) + 1
    nz = int((HOLE_Z_MAX - HOLE_Z_MIN) / 0.1) + 1
    for j in range(nz):
        wz = HOLE_Z_MIN + j * 0.1
        for i in range(nx):
            wx = HOLE_X_MIN + i * 0.1
            wy = sample_height_via_mesh(mesh_y, wx, wz)
            if wy > max_y:
                max_y = wy
                max_xz = (wx, wz)
            if wy > plinth_top:
                pokes.append((wx, wz, wy))
    print(f"[audit] max terrain Y inside hole = {max_y:.4f} at "
          f"({max_xz[0]:+.2f}, {max_xz[1]:+.2f})")
    print(f"[audit] {len(pokes)} grid points poke above plinth top")
    if pokes:
        print("[audit] first 10 poke points:")
        for wx, wz, wy in pokes[:10]:
            print(f"  ({wx:+.2f}, {wz:+.2f}) y={wy:.4f}  delta={wy - plinth_top:+.4f}")

    # 4) Specific corner samples - SW, SE, NW, NE.
    print("\n[audit] hole corners:")
    for label, wx, wz in [
        ("SW", HOLE_X_MIN, HOLE_Z_MAX),  # near-left
        ("SE", HOLE_X_MAX, HOLE_Z_MAX),  # near-right
        ("NW", HOLE_X_MIN, HOLE_Z_MIN),  # far-left
        ("NE", HOLE_X_MAX, HOLE_Z_MIN),  # far-right
    ]:
        wy = sample_height_via_mesh(mesh_y, wx, wz)
        poke = "POKE" if wy > plinth_top else ""
        print(f"  {label}: ({wx:+.2f}, {wz:+.2f})  y={wy:.4f}  "
              f"vs plinth_top {plinth_top:.4f}  {poke}")


if __name__ == "__main__":
    main()
