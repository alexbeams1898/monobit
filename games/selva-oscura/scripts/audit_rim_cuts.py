#!/usr/bin/env python3
"""Standalone audit of the disc-rim heightmap.

Reads:
  - assets/world/terrain/config.json     (region origin / extent / range / y_offset)
  - assets/world/terrain/limbo.png        (the baked heightmap)
  - assets/world/terrain/_world_registry.generated.json (cuts_rim footprints)

For each cuts_rim footprint, scans a dense XZ grid covering the footprint
plus a 20m margin and prints:
  png_raw     — the actual PNG pixel value [0..255] at the nearest pixel
  decoded_y   — that pixel decoded to world Y via the region's range + y_offset
  inside_fp   — whether this XZ is inside any cuts_rim footprint

This is the bake-time view: what the heightmap PNG ACTUALLY contains
after gen_terrain_heightmap.py ran. Compare against rim-audit.log
(runtime view) to spot pipeline-vs-engine mismatches per the
feedback_audit_script_can_lie doctrine.

Run from repo root:
    python games/selva-oscura/scripts/audit_rim_cuts.py
"""

import json
import os
import struct
import sys
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
TERRAIN_DIR = REPO_ROOT / "games" / "selva-oscura" / "assets" / "world" / "terrain"
CONFIG_PATH = TERRAIN_DIR / "config.json"
REGISTRY_PATH = TERRAIN_DIR / "_world_registry.generated.json"


def read_png_grayscale(path):
    """Bare-minimum PNG decoder: 8-bit grayscale only, matches what
    gen_terrain_heightmap.py writes."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    i = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while i < len(data):
        length = struct.unpack(">I", data[i:i+4])[0]
        tag = data[i+4:i+8]
        chunk_data = data[i+8:i+8+length]
        if tag == b"IHDR":
            width, height = struct.unpack(">II", chunk_data[:8])
            bit_depth, color_type = chunk_data[8], chunk_data[9]
        elif tag == b"IDAT":
            idat += chunk_data
        elif tag == b"IEND":
            break
        i += 8 + length + 4
    if bit_depth != 8 or color_type != 0:
        raise SystemExit(f"{path}: expected 8-bit grayscale, got bit_depth={bit_depth} ct={color_type}")
    raw = zlib.decompress(bytes(idat))
    pixels = bytearray(width * height)
    src = 0
    dst = 0
    for _ in range(height):
        filter_type = raw[src]; src += 1
        if filter_type != 0:
            raise SystemExit(f"{path}: only PNG filter=0 supported, row had {filter_type}")
        pixels[dst:dst+width] = raw[src:src+width]
        src += width
        dst += width
    return width, height, bytes(pixels)


def main():
    cfg = json.load(open(CONFIG_PATH, "r", encoding="utf-8"))
    reg = json.load(open(REGISTRY_PATH, "r", encoding="utf-8"))

    regions = cfg["regions"]
    cuts_rim_fps = [f for f in reg.get("structures", []) if f.get("cuts", {}).get("rim")]
    print(f"# audit_rim_cuts.py — found {len(cuts_rim_fps)} cuts_rim footprints")
    print(f"# registry schema_version={reg.get('schema_version')}")
    print()

    for fp in cuts_rim_fps:
        region_name = fp.get("region")
        rcfg = regions.get(region_name)
        if rcfg is None:
            print(f"SKIP {fp.get('name')}: region '{region_name}' not in config.json")
            continue

        png_path = REPO_ROOT / rcfg["heightmap"]
        if not png_path.exists():
            print(f"SKIP {fp.get('name')}: PNG not found at {png_path}")
            continue
        w, h, pixels = read_png_grayscale(png_path)
        origin_x, origin_z = rcfg["world_origin"]
        extent = rcfg["world_extent"]
        half = extent * 0.5
        y_lo = rcfg["height_range_min"]
        y_hi = rcfg["height_range_max"]
        y_off = rcfg.get("y_offset", 0.0)
        y_range = y_hi - y_lo

        cx, cz = fp["center_xz"]
        hx, hz = fp["half_extents_xz"]
        print(f"===== footprint '{fp.get('name')}' region={region_name} "
              f"center=({cx:.2f},{cz:.2f}) half=({hx:.2f},{hz:.2f}) =====")
        print(f"# region {region_name}: origin=({origin_x},{origin_z}) extent={extent} "
              f"hm={w}x{h} y_range=[{y_lo},{y_hi}] y_offset={y_off}")

        # Also report the rim radius + blend so we can sanity-check
        # where the rim is rising.
        if "playable_radius" in rcfg:
            print(f"# (config has no playable_radius — bake-time only in script)")
        # The runtime config DOES NOT carry playable_radius/rim_blend;
        # those live in gen_terrain_heightmap.py. Print what we'd
        # expect if rim is at distance(disc_center, x,z) == radius.
        print()

        kStep = 2.0
        kMargin = 20.0
        x_lo = cx - hx - kMargin
        x_hi = cx + hx + kMargin
        z_lo = cz - hz - kMargin
        z_hi = cz + hz + kMargin

        z = z_lo
        while z <= z_hi:
            x = x_lo
            while x <= x_hi:
                u = (x - (origin_x - half)) / extent
                v = (z - (origin_z - half)) / extent
                inside_png = (0.0 <= u <= 1.0 and 0.0 <= v <= 1.0)
                png_raw = -1
                decoded_y = 0.0
                if inside_png:
                    px = int(u * (w - 1))
                    py = int(v * (h - 1))
                    png_raw = pixels[py * w + px]
                    decoded_y = y_lo + (png_raw / 255.0) * y_range + y_off
                inside_fp = (abs(x - cx) <= hx and abs(z - cz) <= hz)
                disc_dx = x - origin_x
                disc_dz = z - origin_z
                disc_d = (disc_dx*disc_dx + disc_dz*disc_dz) ** 0.5
                print(f"({x:7.2f},{z:7.2f}) | png={png_raw:3d} decoded_y={decoded_y:7.3f} "
                      f"| inside_fp={1 if inside_fp else 0} | disc_dist={disc_d:7.2f}")
                x += kStep
            print()
            z += kStep
        print()


if __name__ == "__main__":
    main()
