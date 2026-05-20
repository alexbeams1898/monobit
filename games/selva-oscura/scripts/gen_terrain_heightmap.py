#!/usr/bin/env python3
"""Generate placeholder heightmaps for Selva Oscura terrain regions.

Output: PNG grayscale (8-bit). Pixel intensity 0..255 maps linearly
to a Y range declared per region in terrain config; runtime decodes
via the region's `height_range_min` and `height_range_max`.

selva_inner region shape (Z axis runs SOUTH from spawn into the
colle):
  Z =  0          : spawn (deep wood floor, Y=0)
  Z = 0 to -90    : flat wood floor, gentle undulation noise
  Z = -90 to -100 : gentle approach ramp, terrain rises ~2m
  Z = -100 to -115: steep colle side, terrain climbs to +13m
  Z = -115 to -130: flat plateau (gothic hub area)
  Z = -130 and beyond: terrain drops back into wood floor
  Stairwell: -3m rectangular depression at (0, -110), 6m wide
"""

import argparse
import math
import os
import struct
import zlib
from pathlib import Path


SELVA_INNER = {
    "name": "selva_inner",
    "resolution": 512,
    "world_extent": 512.0,        # 1m per pixel
    "world_origin_x": 0.0,        # heightmap centered at (origin_x, origin_z)
    "world_origin_z": -130.0,     # centered on the colle ridge
    "height_min": -5.0,
    "height_max": 16.0,

    # Wood floor undulation.
    "noise_amp": 0.5,
    "noise_freq": 0.06,

    # The colle is a SYMMETRIC ridge along the spine direction (-Z):
    # gentle approach ramp -> steep climb -> plateau -> steep descent
    # -> gentle ramp down -> wood floor again on the back side. Wood
    # extends past the colle to the heightmap boundary so the player
    # never sees a hard map edge through the distance fog.
    #
    # Approach side (from spawn):
    "valley_to_ramp_z": -80.0,
    "ramp_to_colle_z": -90.0,
    "colle_top_start_z": -125.0,
    # Plateau:
    "colle_top_end_z": -160.0,
    # Back-side descent (mirror of approach):
    "ramp_from_colle_z": -195.0,  # Z where steep descent ends
    "ramp_to_valley_z": -205.0,   # Z where gentle ramp ends, wood floor resumes
    "approach_rise": 2.0,
    "plateau_height": 9.0,

    # Lateral falloff: the colle is a ridge along X=0; far X taper
    # back into wood-floor terrain.
    "colle_lateral_half_width": 25.0,
    "colle_lateral_falloff": 12.0,
}


def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def colle_centerline_height(z, r):
    """Height along the spine (X=0) as a function of Z, ignoring
    the lateral falloff and noise. Returns world Y meters.

    The ridge is symmetric: the back side mirrors the approach,
    descending to wood floor again so the colle is an island of
    elevation surrounded by wood on all sides.
    """
    # Approach side (spawn-side, +Z of plateau):
    if z >= r["valley_to_ramp_z"]:
        return 0.0
    if z >= r["ramp_to_colle_z"]:
        t = smoothstep(r["valley_to_ramp_z"], r["ramp_to_colle_z"], z)
        return r["approach_rise"] * t
    if z >= r["colle_top_start_z"]:
        t = smoothstep(r["ramp_to_colle_z"], r["colle_top_start_z"], z)
        return r["approach_rise"] + (r["plateau_height"] - r["approach_rise"]) * t
    # Plateau top:
    if z >= r["colle_top_end_z"]:
        return r["plateau_height"]
    # Back side (mirror of approach), descending:
    if z >= r["ramp_from_colle_z"]:
        t = smoothstep(r["ramp_from_colle_z"], r["colle_top_end_z"], z)
        return r["approach_rise"] + (r["plateau_height"] - r["approach_rise"]) * t
    if z >= r["ramp_to_valley_z"]:
        t = smoothstep(r["ramp_to_valley_z"], r["ramp_from_colle_z"], z)
        return r["approach_rise"] * t
    return 0.0


def lateral_factor(x, r):
    """How much of the colle's centerline height applies at a given
    X. 1 = full plateau height, 0 = wood floor."""
    abs_x = abs(x)
    if abs_x <= r["colle_lateral_half_width"]:
        return 1.0
    far_x = r["colle_lateral_half_width"] + r["colle_lateral_falloff"]
    if abs_x >= far_x:
        return 0.0
    t = (abs_x - r["colle_lateral_half_width"]) / r["colle_lateral_falloff"]
    return 1.0 - t * t * (3.0 - 2.0 * t)  # smoothstep falloff


def world_height(x, z, r):
    """Compute world-space Y for a given (x, z) under region rules."""
    # Base colle profile.
    centerline = colle_centerline_height(z, r)
    lateral = lateral_factor(x, r)
    base = centerline * lateral
    # Wood floor undulation, attenuated on top of the plateau so the
    # hub reads as "worn smooth by foot traffic."
    nx = math.sin(x * r["noise_freq"])
    nz = math.cos(z * r["noise_freq"])
    undulation_strength = 1.0 - 0.7 * smoothstep(0.0, r["plateau_height"], base)
    undulation = nx * nz * r["noise_amp"] * undulation_strength
    return base + undulation


def pixel_to_world(px, py, res, extent, origin_x, origin_z):
    half = extent * 0.5
    x = (px / (res - 1)) * extent - half + origin_x
    z = (py / (res - 1)) * extent - half + origin_z
    return x, z


def height_to_pixel(y, region):
    lo, hi = region["height_min"], region["height_max"]
    t = (y - lo) / (hi - lo)
    t = max(0.0, min(1.0, t))
    return int(round(t * 255))


def write_png_grayscale(path, width, height, pixels):
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    raw = bytearray()
    for row in range(height):
        raw.append(0)
        start = row * width
        raw.extend(pixels[start : start + width])
    idat = zlib.compress(bytes(raw), 9)
    out = sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def generate_region(region, out_path):
    res = region["resolution"]
    extent = region["world_extent"]
    ox = region.get("world_origin_x", 0.0)
    oz = region.get("world_origin_z", 0.0)
    pixels = bytearray(res * res)
    for py in range(res):
        for px in range(res):
            x, z = pixel_to_world(px, py, res, extent, ox, oz)
            y = world_height(x, z, region)
            pixels[py * res + px] = height_to_pixel(y, region)
    write_png_grayscale(out_path, res, res, pixels)
    print(f"[gen_terrain] wrote {out_path} ({res}x{res}, {extent:.0f}m extent, "
          f"origin=({ox:.0f},{oz:.0f}))")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        default=os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "assets", "world", "terrain"
        ),
    )
    args = parser.parse_args()
    out_dir = os.path.abspath(args.out_dir)
    generate_region(SELVA_INNER, os.path.join(out_dir, "selva_inner.png"))


if __name__ == "__main__":
    main()
