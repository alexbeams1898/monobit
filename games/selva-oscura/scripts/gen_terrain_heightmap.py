#!/usr/bin/env python3
"""Generate placeholder heightmaps for Selva Oscura terrain regions.

Output: PNG grayscale (8-bit). Pixel intensity 0..255 maps linearly
to a Y range declared per region in terrain config; runtime decodes
via the region's `height_range_min` and `height_range_max`.

selva_inner region shape (Z axis runs SOUTH from spawn into the
colle):
  Z =  0           : spawn (wake-zone floor, Y=-3 - depressed)
  Z = 0 to -80     : wake-zone basin (Y~=-3) with gentle noise
  Z = -80 to -110  : gentle approach ramp, terrain rises Y=-3 -> Y=+5
  Z = -110 to -190 : steep colle side, terrain climbs Y=+5 -> Y=+35
  Z = -190 to -230 : flat plateau (the dilettoso monte summit)
  Z = -230 and beyond: terrain descends back into wood floor

Lateral (X axis): the wake-zone basin is surrounded on E/W/N sides
by a gentle 4m berm rising starting at +-80m from the wake-zone
center, peaking around Y=+1 to +4. Combined with the depressed
wake-zone floor, the player at spawn is at Y=-3 with surrounding
terrain at Y=0 to +4 in every direction except the colle (much
higher), producing the canonical "bottom of a valley" feeling that
Dante's text implies (Inferno I: the Wood ends "la dove terminava
quella valle" - where the valley ended - and the colle rises out
of it).

The colle is the mecca: its 38m total relief from wake-zone floor
to plateau is ~4x what the v0 heightmap had. Subtends roughly 17deg
of vertical visual angle from the wake-zone, making it dominant on
the horizon while staying modest in absolute terms (the size of a
10-12 story building - a real central-Italian colle, not an alpine
peak).
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
    "world_origin_z": -180.0,     # centered on the colle ridge (shifted south
                                  # to fit the longer steep-climb segment)
    "height_min": -6.0,
    "height_max": 38.0,

    # Wake-zone floor depression: the player at spawn (Z=0) is at Y=-3,
    # below the surrounding terrain. Wood floor undulation rides on
    # top of this depressed baseline.
    "wake_zone_y": -3.0,
    "noise_amp": 0.5,
    "noise_freq": 0.06,

    # E/W/N surround berm: terrain rises gently away from the wake-zone
    # center on the three sides not facing the colle. Combined with the
    # depressed wake-zone, the player is in a basin.
    # Rise starts at +-80m from origin in X (lateral) and at +30m in Z
    # (behind the spawn, +Z side). Peaks Y=+4 at +-200m / +180m.
    "surround_rise_start_lateral": 80.0,
    "surround_rise_peak_lateral": 200.0,
    "surround_rise_start_behind_z": 30.0,   # +Z side (behind spawn)
    "surround_rise_peak_behind_z": 180.0,
    "surround_peak_height": 4.0,

    # The colle is a SYMMETRIC ridge along the spine direction (-Z):
    # gentle approach ramp -> long steep climb -> plateau -> steep
    # descent -> gentle ramp down -> wood floor again on the back
    # side. The plateau is the mecca - dominant on the horizon at 35m
    # of relief above wake-zone floor (38m total height_max).
    #
    # Approach side (from spawn):
    "valley_to_ramp_z": -20.0,    # gentle ramp starts here
                                  # (very close to spawn, was -80)
    "ramp_to_colle_z": -60.0,     # gentle ramp ends, climb begins
                                  # (40m gentle ramp)
    "colle_top_start_z": -190.0,  # climb ends, plateau starts
                                  # (climb is now 130m for the same 30m
                                  # rise: ~13deg, countryside gentle)
    # Plateau (40m wide along the spine):
    "colle_top_end_z": -230.0,
    # Back-side descent (mirror of approach):
    "ramp_from_colle_z": -310.0,  # steep descent ends
    "ramp_to_valley_z": -340.0,   # gentle ramp ends, wood floor resumes
    "approach_rise": 5.0,         # was 2.0; gentler-into-steeper-feel
    "plateau_height": 35.0,       # was 9.0; THE big change (~4x relief)

    # Lateral falloff: the colle is a ridge along X=0; far X taper
    # back into wood-floor terrain.
    "colle_lateral_half_width": 35.0,  # was 25.0; plateau is wider too
    "colle_lateral_falloff": 18.0,     # was 12.0; gentler shoulders
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


def wake_zone_floor(x, z, r):
    """Baseline wood-floor height for points OFF the colle. Combines the
    wake-zone depression near spawn with the E/W/N surround berm that
    rises far from spawn on the non-colle sides. Returns world Y meters.

    The "wake-zone" is roughly the spawn-side basin (positive Z, near
    the X=0 spine). It sits at wake_zone_y (-3m by default). Moving
    laterally (|x| growing) or back behind spawn (+Z growing past the
    surround_rise_start_behind_z), the floor rises smoothly to
    surround_peak_height (+4m by default), producing a basin the player
    feels enclosed by on three sides. The colle side (-Z) does NOT get
    this surround rise - it transitions into the colle ramp directly.
    """
    # Lateral rise (|x| > start): both sides of the spine.
    abs_x = abs(x)
    lat_t = smoothstep(
        r["surround_rise_start_lateral"], r["surround_rise_peak_lateral"], abs_x
    )

    # Behind-spawn rise (Z growing in +Z direction past the start).
    # Only applies on the spawn side (Z > 0); the colle side gets the
    # colle ramp directly.
    if z > 0:
        behind_t = smoothstep(
            r["surround_rise_start_behind_z"], r["surround_rise_peak_behind_z"], z
        )
    else:
        behind_t = 0.0

    # Combine: use the stronger of the two contributions (max), so the
    # corners (high x AND high z) don't double-rise into a peak.
    surround_t = max(lat_t, behind_t)
    rise = surround_t * (r["surround_peak_height"] - r["wake_zone_y"])
    return r["wake_zone_y"] + rise


def world_height(x, z, r):
    """Compute world-space Y for a given (x, z) under region rules."""
    # Wake-zone basin / surround berm: the floor away from the colle.
    floor = wake_zone_floor(x, z, r)

    # Colle profile (rises along the spine), measured relative to floor.
    centerline = colle_centerline_height(z, r)
    lateral = lateral_factor(x, r)
    colle_above_floor = centerline * lateral

    base = floor + colle_above_floor

    # Wood floor undulation, attenuated on top of the plateau so the
    # hub reads as "worn smooth by foot traffic."
    nx = math.sin(x * r["noise_freq"])
    nz = math.cos(z * r["noise_freq"])
    undulation_strength = 1.0 - 0.7 * smoothstep(0.0, r["plateau_height"], colle_above_floor)

    # No architecture-specific flattening here — chapel plateau /
    # other terrain deformations are applied at RUNTIME via the
    # engine::world::TerrainModifiers registry (see CryptLayout.h
    # registerChapelTerrainModifiers). The heightmap encodes ONLY the
    # natural terrain shape (colle, wake-zone basin, noise). Single
    # source of truth: heightmap = natural geology, registry =
    # architecture-driven shaping.

    undulation = nx * nz * r["noise_amp"] * undulation_strength
    surface = base + undulation

    return surface


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
