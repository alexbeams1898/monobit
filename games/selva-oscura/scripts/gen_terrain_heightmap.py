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
import json
import math
import os
import struct
import zlib
from pathlib import Path


# Registry of all terrain regions for Selva Oscura. Add a new entry here
# to bake a new region (run with --region NAME or bake all). Each
# region's keys correspond 1:1 with what gen_terrain_heightmap.py needs
# to generate a heightmap PNG; the actual placement in world space is
# stored in the engine's terrain config and read at runtime.
REGIONS = {}


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
    "approach_rise": 5.0,         # gentler-into-steeper-feel
    # Plateau height — delta ABOVE wake_zone_y (-3), not absolute Y.
    # Plateau world Y = wake_zone_y + plateau_height = -3 + 25 = 22.
    # Chapel ground at world Y=22.5 sits ~50cm above this, so the
    # foundation skirt is barely visible — chapel reads as built ON
    # the hilltop with a low stone base.
    "plateau_height": 25.0,
    # Lateral falloff: the colle is a ridge along X=0; far X taper back
    # into wood-floor terrain. Falloff widened (was 18) so the lateral
    # sides feel like hill slopes, not cliffs. Half-width narrowed (was
    # 35) so the gentle slope starts sooner — the plateau is a modest
    # ridgetop rather than a wide flat mesa.
    "colle_lateral_half_width": 15.0,
    "colle_lateral_falloff": 60.0,
}
REGIONS["selva_inner"] = SELVA_INNER


# Limbo (First Circle of Hell). Disc-shaped per the Inferno-vertical-
# stack doctrine (canonical cosmology = concentric circles, narrowing
# toward Lucifer). Square heightmap holds a disc of radius
# LIMBO_RADIUS; everything outside that radius is raised to the ceiling
# to form a continuous rock wall around the playable disc. Each
# successive circle (Lust, Gluttony, ..., Cocytus) reuses this same
# kind ("disc") with a smaller radius — see RADIUS TABLE in
# docs/design/inferno_stack.md (forthcoming).
LIMBO_RADIUS = 300.0  # meters; disc half-width of the playable area
LIMBO_RIM_BLEND = 12.0  # smooth rise band so the disc edge isn't a vertical cliff
LIMBO = {
    "name": "limbo",
    "kind": "disc",
    "resolution": 512,
    # Square holds the disc PLUS a full rim_blend strip on every
    # cardinal side, so the rock-collar wraps the disc continuously
    # (no flat lateral-wall sections on cardinal axes where the disc
    # edge would otherwise meet the square edge with no room for the
    # collar). Total half-extent = radius + rim_blend = 312m.
    "world_extent": 2.0 * (LIMBO_RADIUS + LIMBO_RIM_BLEND),
    # Descent stair lets out near the disc's north rim, inset 30m so
    # the player isn't on the wall. Disc center sits 30m + radius
    # south of the stair landing at Z = -351.15.
    #   stair_land_z = -351.15
    #   disc_center_z = stair_land_z - (radius - inset_from_rim)
    #                 = -351.15 - (300 - 30) = -621.15
    "world_origin_x": 0.0,
    "world_origin_z": -621.15,
    "playable_radius": LIMBO_RADIUS,
    "rim_blend": LIMBO_RIM_BLEND,
    # Heightmap range: 0 (disc floor) → 1 (rim/rock-collar peak). The
    # runtime y_offset in config.json places the disc floor at world
    # Y = -43.13; the rock-collar tops out at y_offset + height_max,
    # which needs to reach ceiling Y (-7.0) for a clean wall-ceiling
    # join. So height_max = ceiling_y - y_offset = -7 - (-43.13) = 36.13.
    "height_min": 0.0,
    "height_max": 36.13,
    "subdivide": 300,             # ~2m per quad — finer than the v0 limbo
                                  # because the disc is much larger
                                  # and the rim curve needs smooth verts.
    "base_color": [0.05, 0.04, 0.04],
}
REGIONS["limbo"] = LIMBO


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
    """Compute world-space Y for a given (x, z) under region rules.

    Branches on the region's `kind`:
      - "selva_surface" — colle + wake-zone basin + noise (current
        selva_inner generator).
      - "flat" — featureless plateau at 0 (used for underground layers
        like Limbo where the entire shape comes from runtime terrain
        modifiers; y_offset on the TerrainRegion places the layer in
        world Y).
    """
    kind = r.get("kind", "selva_surface")

    if kind == "flat":
        return 0.0

    if kind == "disc":
        # Disc-shaped layer: floor sits at height_min everywhere inside
        # `playable_radius`, then rises smoothly over `rim_blend` meters
        # to height_max past the radius. Past (radius + rim_blend) the
        # PNG saturates at height_max — the rock collar that fills the
        # square's corners. The rim_blend strip is what the player sees
        # as the cavern wall curving up from the disc floor.
        #
        # Structure footprints with cuts_rim=true (e.g. the descent
        # corridor piercing Limbo's north edge) keep the rim at floor
        # Y inside their rect. Source: _world_registry.generated.json,
        # written by the dump-world C++ binary so the rim agrees with
        # the runtime ceiling/wall/discard consumers.
        for f in r.get("structures_cuts_rim", []):
            cx, cz = f["center_xz"]
            hx, hz = f["half_extents_xz"]
            if abs(x - cx) <= hx and abs(z - cz) <= hz:
                return r["height_min"]

        radius = r["playable_radius"]
        rim_blend = r.get("rim_blend", 0.0)
        dx = x - r.get("world_origin_x", 0.0)
        dz = z - r.get("world_origin_z", 0.0)
        d = math.sqrt(dx * dx + dz * dz)
        if d <= radius:
            return r["height_min"]
        if rim_blend <= 0.0 or d >= radius + rim_blend:
            return r["height_max"]
        t = smoothstep(radius, radius + rim_blend, d)
        return r["height_min"] + (r["height_max"] - r["height_min"]) * t

    # Default: selva_surface generator.
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


def load_world_registry(out_dir):
    """Load `_world_registry.generated.json` (written by the
    selva-oscura-dump-world binary) and bucket cuts_rim footprints
    onto their target region. The rim function reads
    `structures_cuts_rim` from each region's dict.

    The generated file is the C++ runtime registry serialized at
    build time — single source of truth across runtime engine and
    bake-time tools. CMake wires the dump binary as a dependency of
    this script's target so the file is always current.

    Silent if the file is missing (first-bake bootstrap before the
    engine target builds — the heightmap baker will produce a no-
    cuts result; the next build cycle rebakes with the registry in
    place).
    """
    reg_path = os.path.join(out_dir, "_world_registry.generated.json")
    if not os.path.exists(reg_path):
        print(f"[gen_terrain] world registry missing: {reg_path} (first-bake bootstrap)")
        return
    with open(reg_path, "r", encoding="utf-8") as fp:
        reg = json.load(fp)
    schema = reg.get("schema_version", 0)
    if schema != 1:
        raise SystemExit(
            f"[gen_terrain] unsupported world-registry schema_version={schema} "
            f"(expected 1). Rebuild selva-oscura-dump-world or update this script."
        )
    for f in reg.get("structures", []):
        region = f.get("region")
        if region not in REGIONS:
            continue
        cuts = f.get("cuts", {})
        if cuts.get("rim"):
            REGIONS[region].setdefault("structures_cuts_rim", []).append(f)


def sample_structure_slot(f, x, z):
    """Mirror of engine::world::sampleStructureSlot. Returns
    (y_min, y_max) or None if no slot at this XZ.
    """
    vp = f.get("vertical_profile")
    if vp is None:
        return None
    nx = vp.get("nx", 0)
    nz = vp.get("nz", 0)
    cells = vp.get("cells", [])
    if nx <= 0 or nz <= 0 or len(cells) != nz:
        return None
    cx, cz = f["center_xz"]
    hx, hz = f["half_extents_xz"]
    if abs(x - cx) > hx or abs(z - cz) > hz:
        return None
    u = (x - (cx - hx)) / (2.0 * hx)
    v = (z - (cz - hz)) / (2.0 * hz)
    fx = u * (nx - 1)
    fz = v * (nz - 1)
    ix0 = max(0, min(nx - 1, int(math.floor(fx))))
    iz0 = max(0, min(nz - 1, int(math.floor(fz))))
    ix1 = min(ix0 + 1, nx - 1)
    iz1 = min(iz0 + 1, nz - 1)
    tx = max(0.0, min(1.0, fx - ix0))
    tz = max(0.0, min(1.0, fz - iz0))

    def cell(ix, iz):
        return cells[iz][ix]

    c00, c10, c01, c11 = cell(ix0, iz0), cell(ix1, iz0), cell(ix0, iz1), cell(ix1, iz1)
    # Empty cells: y_min > y_max sentinel.
    for c in (c00, c10, c01, c11):
        if c[0] > c[1]:
            return None
    ymin_x0 = c00[0] + (c01[0] - c00[0]) * tz
    ymin_x1 = c10[0] + (c11[0] - c10[0]) * tz
    ymax_x0 = c00[1] + (c01[1] - c00[1]) * tz
    ymax_x1 = c10[1] + (c11[1] - c10[1]) * tz
    return (ymin_x0 + (ymin_x1 - ymin_x0) * tx,
            ymax_x0 + (ymax_x1 - ymax_x0) * tx)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        default=os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "assets", "world", "terrain"
        ),
    )
    parser.add_argument(
        "--region",
        default=None,
        help="Region name to bake (one of: " + ", ".join(sorted(REGIONS.keys())) + "). "
             "If omitted, bakes all registered regions.",
    )
    args = parser.parse_args()
    out_dir = os.path.abspath(args.out_dir)
    load_world_registry(out_dir)

    if args.region is None:
        targets = list(REGIONS.values())
    else:
        if args.region not in REGIONS:
            parser.error(f"Unknown region '{args.region}'. Available: "
                         + ", ".join(sorted(REGIONS.keys())))
        targets = [REGIONS[args.region]]

    for region in targets:
        out_path = os.path.join(out_dir, f"{region['name']}.png")
        generate_region(region, out_path)


if __name__ == "__main__":
    main()
