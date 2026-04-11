"""
Palette baker for LPC masters.

Inputs
------
  game/assets/sprites/lpc/raw/<layer>/<style>/master/<anim>.png     (master sprites)
  game/assets/sprites/lpc/raw/_palettes/<palette>_ulpc.json         (color ramps)

Outputs
-------
  game/assets/sprites/lpc/raw/<layer>/<style>/<our_color>/<anim>.png

How it works
------------
Each master PNG is drawn using a single canonical source ramp from the LPC
palette definitions:

  body / head     -> body_ulpc["light"]
  torso / legs    -> cloth_ulpc["white"]

For each target color we map source ramp pixels to target ramp pixels index-
by-index and rewrite the PNG.

Body and head masters include baked "blue" eye pixels. We leave those alone;
our eye layer composites on top and fully overdraws them at render time.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is required. Install with `pip install Pillow`.")
    sys.exit(1)


LPC_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "assets", "sprites", "lpc", "raw",
)

PALETTE_DIR = os.path.join(LPC_ROOT, "_palettes")

ANIMATIONS = ["walk", "run", "slash", "hurt"]


def load_palette(name: str) -> dict:
    with open(os.path.join(PALETTE_DIR, f"{name}_ulpc.json"), encoding="utf-8") as handle:
        return json.load(handle)


def hex_to_rgb(hex_str: str) -> tuple:
    s = hex_str.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


# --------------------------------------------------------------------------- #
# Bake manifest
# --------------------------------------------------------------------------- #
#
# Each entry maps a master layer/style to:
#   palette     -- which palette file to use ("body", "cloth", ...)
#   source_key  -- which ramp in that palette the master is drawn with
#   variants    -- list of (our_color_id, target_ramp_key)
#
# Layers whose style directory doesn't contain a "master/" subdir are skipped.

SKIN_TONES = [
    ("tone_1",  "light"),
    ("tone_2",  "amber"),
    ("tone_3",  "olive"),
    ("tone_4",  "taupe"),
    ("tone_5",  "bronze"),
    ("tone_6",  "brown"),
    ("tone_7",  "black"),
    # Fun / non-human tones for demonic / supernatural characters.
    ("tone_8",  "bright_green"),
    ("tone_9",  "dark_green"),
    ("tone_10", "blue"),
    ("tone_11", "lavender"),
    ("tone_12", "zombie_green"),
]

CLOTH_COLORS = [
    ("white",  "white"),
    ("black",  "black"),
    ("gray",   "gray"),
    ("brown",  "brown"),
    ("tan",    "tan"),
    ("red",    "red"),
    ("orange", "orange"),
    ("yellow", "yellow"),
    ("green",  "green"),
    ("blue",   "blue"),
    ("purple", "purple"),
    ("pink",   "pink"),
]

BAKE_MANIFEST = [
    # Body -> 7 skin tones from body_ulpc.
    {"layer": "body", "style": "male",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},

    # All 5 head variants -> 7 skin tones.
    {"layer": "head", "style": "base",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},
    {"layer": "head", "style": "elderly",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},
    {"layer": "head", "style": "gaunt",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},
    {"layer": "head", "style": "plump",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},
    {"layer": "head", "style": "small",
     "palette": "body", "source_key": "light", "variants": SKIN_TONES},

    # Torso clothes -> 12 cloth colors.
    {"layer": "torso", "style": "shortsleeve",
     "palette": "cloth", "source_key": "white", "variants": CLOTH_COLORS},
    {"layer": "torso", "style": "longsleeve",
     "palette": "cloth", "source_key": "white", "variants": CLOTH_COLORS},

    # Legs -> 12 cloth colors.
    {"layer": "legs", "style": "pants",
     "palette": "cloth", "source_key": "white", "variants": CLOTH_COLORS},
    {"layer": "legs", "style": "shorts",
     "palette": "cloth", "source_key": "white", "variants": CLOTH_COLORS},
]

# --------------------------------------------------------------------------- #
# Recoloring
# --------------------------------------------------------------------------- #


def bake_variant(master_img: Image.Image, src_rgb: list, dst_rgb: list) -> Image.Image:
    """Return a recolored copy of master_img with src_rgb -> dst_rgb mapping."""
    rgba = master_img.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size

    # Build a direct lookup for speed.
    lut = {src: dst for src, dst in zip(src_rgb, dst_rgb)}

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            mapped = lut.get((r, g, b))
            if mapped is not None:
                pixels[x, y] = (mapped[0], mapped[1], mapped[2], a)

    return rgba


def bake_layer(entry: dict, force: bool) -> None:
    layer = entry["layer"]
    style = entry["style"]
    palette_name = entry["palette"]
    source_key = entry["source_key"]
    variants = entry["variants"]

    master_dir = os.path.join(LPC_ROOT, layer, style, "master")
    if not os.path.isdir(master_dir):
        print(f"  SKIP {layer}/{style}: no master dir")
        return

    palette = load_palette(palette_name)
    src_rgb = [hex_to_rgb(c) for c in palette[source_key]]

    for our_color, target_key in variants:
        target_ramp = palette.get(target_key)
        if target_ramp is None:
            print(f"  FAIL {layer}/{style}: missing target '{target_key}' in {palette_name}_ulpc.json")
            continue
        dst_rgb = [hex_to_rgb(c) for c in target_ramp]

        out_dir = os.path.join(LPC_ROOT, layer, style, our_color)
        os.makedirs(out_dir, exist_ok=True)

        for anim in ANIMATIONS:
            master_path = os.path.join(master_dir, f"{anim}.png")
            out_path = os.path.join(out_dir, f"{anim}.png")
            if not os.path.exists(master_path):
                print(f"  MISS {master_path}")
                continue
            if os.path.exists(out_path) and not force:
                continue
            master_img = Image.open(master_path)
            baked = bake_variant(master_img, src_rgb, dst_rgb)
            baked.save(out_path)
            print(f"  OK   {layer}/{style}/{our_color}/{anim}.png")


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Bake LPC master PNGs into color variants.")
    parser.add_argument("--force", action="store_true", help="re-bake even if output exists")
    args = parser.parse_args()

    print(f"LPC root: {LPC_ROOT}")
    for entry in BAKE_MANIFEST:
        bake_layer(entry, args.force)
    print("\nDone.")


if __name__ == "__main__":
    main()
