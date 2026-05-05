"""
Fetch LPC source art from LiberatedPixelCup/Universal-LPC-Spritesheet-Character-Generator.

Why this fork
-------------
LiberatedPixelCup is the actively maintained LPC generator. It ships:
  * Per-animation PNG files: walk.png, run.png, slash.png, hurt.png, idle.png
  * A consistent masculine body type across body/head/torso/legs/feet
  * A wide hair + beard library with proper run animations
  * Palette definition files for runtime/build-time color swap on clothing masters
  * Pre-baked color variants for hair, beards, eyes, footwear

Directory patterns (observed in upstream repo)
----------------------------------------------
  body masters     body/bodies/male/<anim>.png
  head masters     head/heads/human/<variant>/<anim>.png   (variant = male, male_elderly, male_gaunt, male_plump, male_small)
  eyes (colored)   eyes/human/adult/neutral/<anim>/<color>.png
  hair (colored)   hair/<style>/adult/<anim>/<color>.png
  beards (colored) beards/beard/<style>/<anim>/<color>.png
  beard winter     beards/beard/winter/male/<anim>/<color>.png
  mustaches        beards/mustache/<style>/<anim>/<color>.png
  torso masters    torso/clothes/<style>/<style>/male/<anim>.png
  legs pants       legs/pants/male/<anim>.png
  legs shorts      legs/shorts/shorts/male/<anim>.png
  feet shoes       feet/shoes/basic/male/<anim>/<color>.png
  feet boots       feet/boots/basic/male/<anim>/<color>.png

We use the upstream `long` hair style directly (ponytail has no run.png).

Output tree
-----------
games/prison-escape-game/assets/sprites/lpc/raw/<layer>/<style>/<color>/<anim>.png      (pre-baked colors)
games/prison-escape-game/assets/sprites/lpc/raw/<layer>/<style>/master/<anim>.png        (palette-swap masters)
games/prison-escape-game/assets/sprites/lpc/raw/_palettes/<palette>.json                 (baker input)

This script is idempotent; existing files are skipped unless --force.

Usage
-----
  python scripts/fetch_lpc.py             # fetch everything
  python scripts/fetch_lpc.py --force     # redownload
  python scripts/fetch_lpc.py --dry-run   # list URLs without downloading
  python scripts/fetch_lpc.py --only hair # fetch a single layer
"""

import argparse
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Iterable

# --------------------------------------------------------------------------- #
# Repository root
# --------------------------------------------------------------------------- #

LPC_BASE = (
    "https://raw.githubusercontent.com/LiberatedPixelCup/"
    "Universal-LPC-Spritesheet-Character-Generator/master"
)

# Animations we bake into the engine sheet.
# (upstream_filename_stem, our_state_key)
ANIMATIONS = [
    ("walk",          "walk"),
    ("run",           "run"),
    ("slash",         "slash"),
    ("hurt",          "hurt"),
    ("thrust",        "thrust"),
    ("shoot",         "shoot"),
    ("reverse_slash", "reverse_slash"),
]

# --------------------------------------------------------------------------- #
# Color vocabularies
# --------------------------------------------------------------------------- #

# Our hair/beard palette -> upstream color stem.
# These are pre-baked color PNGs from the LPC repo.
HAIR_COLORS = [
    ("black",     "black"),
    ("charcoal",  "dark_gray"),
    ("gray",      "gray"),
    ("brown",     "dark_brown"),
    ("ash_brown", "ash"),
    ("chestnut",  "chestnut"),
    ("blonde",    "blonde"),
    ("red",       "red"),
    ("orange",    "orange"),
    ("pink",      "pink"),
    ("blue",      "blue"),
    ("green",     "green"),
]

# Eye colors (LPC eyes exist under /eyes/human/adult/neutral/<anim>/<color>.png).
# Upstream vocabulary for eyes differs from hair; we pick equivalents.
EYE_COLORS = [
    ("brown", "brown"),
    ("blue",  "blue"),
    ("green", "green"),
    ("gray",  "gray"),
]

# Footwear colors. LPC shoes/boots use a metal+cloth vocabulary (black, brown,
# leather, tan, navy, red, green, ...). We keep our 12-slot vocabulary and map
# to upstream names that actually exist.
FEET_COLORS = [
    ("white",  "white"),
    ("black",  "black"),
    ("gray",   "gray"),
    ("brown",  "brown"),
    ("tan",    "tan"),
    ("red",    "red"),
    ("orange", "orange"),
    ("yellow", "gold"),    # no "yellow" in metal palette -> closest is gold
    ("green",  "green"),
    ("blue",   "blue"),
    ("purple", "purple"),
    ("pink",   "pink"),
]

# --------------------------------------------------------------------------- #
# Layer manifests
# --------------------------------------------------------------------------- #
#
# Each entry:
#   layer    -- our internal layer id
#   style    -- our internal style id
#   path     -- directory containing per-animation PNGs (master) or per-anim
#               subdirs (colored). See "mode".
#   mode     -- "master" = <path>/<anim>.png single file per animation
#               "colored" = <path>/<anim>/<color>.png per-color per-animation
#   colors   -- list of (our_color_id, upstream_color_stem) when mode="colored"

# ---------- Body (master) ----------
BODY_LAYERS = [
    {"layer": "body", "style": "male",
     "path": "spritesheets/body/bodies/male", "mode": "master"},
]

# ---------- Heads (masters, five variants) ----------
HEAD_LAYERS = [
    {"layer": "head", "style": "base",
     "path": "spritesheets/head/heads/human/male", "mode": "master"},
    {"layer": "head", "style": "elderly",
     "path": "spritesheets/head/heads/human/male_elderly", "mode": "master"},
    {"layer": "head", "style": "gaunt",
     "path": "spritesheets/head/heads/human/male_gaunt", "mode": "master"},
    {"layer": "head", "style": "plump",
     "path": "spritesheets/head/heads/human/male_plump", "mode": "master"},
    {"layer": "head", "style": "small",
     "path": "spritesheets/head/heads/human/male_small", "mode": "master"},
]

# ---------- Eyes (colored, expression = neutral) ----------
EYE_LAYERS = [
    {"layer": "eyes", "style": "eyes",
     "path": "spritesheets/eyes/human/adult/neutral",
     "mode": "colored", "colors": EYE_COLORS},
]

# ---------- Hair (colored) ----------
# (upstream_style_dir, our_style_id)
HAIR_STYLES = [
    ("plain",              "plain"),
    ("buzzcut",            "buzzcut"),
    ("spiked",             "spiked"),
    ("messy1",             "messy"),
    ("long",               "long"),
    ("shorthawk",          "shorthawk"),
    ("flat_top_straight",  "flat_top"),
    ("parted",             "parted"),
    ("cowlick",            "cowlick"),
    ("page",               "page"),
    ("curly_short",        "curly"),
    ("idol",               "idol"),
    ("bangs_bun",          "bangs_bun"),
    ("cornrows",           "cornrows"),
    ("dreadlocks_short",   "dreadlocks"),
    ("bob_side_part",      "bob_side"),
    ("bob",                "bob_bangs"),
]

HAIR_LAYERS = [
    {"layer": "hair", "style": our,
     "path": f"spritesheets/hair/{upstream}/adult",
     "mode": "colored", "colors": HAIR_COLORS}
    for upstream, our in HAIR_STYLES
]

# ---------- Facial hair (colored) ----------
# All beards and mustaches use the same `<path>/<anim>/<color>.png` layout.
# Entries: (upstream path, our_style_id)
FACIAL_STYLES = [
    ("beards/beard/basic",           "basic"),
    ("beards/beard/5oclock_shadow",  "shadow"),
    ("beards/beard/winter/male",     "winter"),
    ("beards/beard/trimmed",         "trimmed"),
    ("beards/beard/medium",          "medium"),
    ("beards/mustache/walrus",       "walrus"),
    ("beards/mustache/chevron",      "chevron"),
    ("beards/mustache/handlebar",    "handlebar"),
    ("beards/mustache/bigstache",    "bigstache"),
    ("beards/mustache/french",       "french"),
    ("beards/mustache/horseshoe",    "horseshoe"),
    ("beards/mustache/lampshade",    "lampshade"),
]

FACIAL_LAYERS = [
    {"layer": "facial", "style": our,
     "path": f"spritesheets/{rel}",
     "mode": "colored", "colors": HAIR_COLORS}
    for rel, our in FACIAL_STYLES
]

# ---------- Torso (masters) ----------
TORSO_LAYERS = [
    {"layer": "torso", "style": "shortsleeve",
     "path": "spritesheets/torso/clothes/shortsleeve/shortsleeve/male",
     "mode": "master"},
    {"layer": "torso", "style": "longsleeve",
     "path": "spritesheets/torso/clothes/longsleeve/longsleeve/male",
     "mode": "master"},
]

# ---------- Legs (masters) ----------
LEGS_LAYERS = [
    {"layer": "legs", "style": "pants",
     "path": "spritesheets/legs/pants/male", "mode": "master"},
    {"layer": "legs", "style": "shorts",
     "path": "spritesheets/legs/shorts/shorts/male", "mode": "master"},
]

# ---------- Feet (colored) ----------
FEET_LAYERS = [
    {"layer": "feet", "style": "shoes",
     "path": "spritesheets/feet/shoes/basic/male",
     "mode": "colored", "colors": FEET_COLORS},
    {"layer": "feet", "style": "boots",
     "path": "spritesheets/feet/boots/basic/male",
     "mode": "colored", "colors": FEET_COLORS},
]

# ---------- Headwear (colored) ----------
# Hats and other head armor. Upstream layout is `spritesheets/hat/<group>/<style>/adult/<anim>/<color>.png`.
# Intentionally scoped to hats that have run.png (most formal/visor entries do).
# This layer is the foundation for head-slot armor tracking (separate ticket).
HEADWEAR_COLORS = [
    ("black",  "black"),
    ("gray",   "gray"),
    ("brown",  "brown"),
    ("tan",    "tan"),
    ("white",  "white"),
    ("red",    "red"),
    ("blue",   "blue"),
    ("green",  "green"),
    ("navy",   "navy"),
    ("purple", "purple"),
]

# (upstream relative path under spritesheets/, our_style_id)
HEADWEAR_STYLES = [
    ("hat/formal/bowler/adult",  "bowler"),
    ("hat/formal/tophat/adult",  "tophat"),
]

HEADWEAR_LAYERS = [
    {"layer": "headwear", "style": our,
     "path": f"spritesheets/{rel}",
     "mode": "colored", "colors": HEADWEAR_COLORS}
    for rel, our in HEADWEAR_STYLES
]

ALL_LAYERS = (
    BODY_LAYERS + HEAD_LAYERS + EYE_LAYERS
    + HAIR_LAYERS + FACIAL_LAYERS
    + TORSO_LAYERS + LEGS_LAYERS + FEET_LAYERS
    + HEADWEAR_LAYERS
)

# --------------------------------------------------------------------------- #
# Palette definition files (for build-time baker pass)
# --------------------------------------------------------------------------- #

PALETTE_FILES = [
    "palette_definitions/body/body_ulpc.json",
    "palette_definitions/cloth/cloth_ulpc.json",
    "palette_definitions/hair/hair_ulpc.json",
    "palette_definitions/eye/eye_ulpc.json",
    "palette_definitions/metal/metal_ulpc.json",
]

# --------------------------------------------------------------------------- #
# Fetch mechanics
# --------------------------------------------------------------------------- #

RAW_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "assets", "sprites", "lpc", "raw",
)

PALETTE_ROOT = os.path.join(RAW_ROOT, "_palettes")

THROTTLE_SEC = 0.05


def download(url: str, dest: str, force: bool, dry_run: bool) -> bool:
    """Return True if a new file was written (or would be in dry-run)."""
    if os.path.exists(dest) and not force:
        return False
    if dry_run:
        print(f"  DRY  {url}")
        return True
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "lpc-fetch"})
        with urllib.request.urlopen(req, timeout=30) as response:
            data = response.read()
    except urllib.error.HTTPError as exc:
        print(f"  FAIL {url}  [{exc.code}]")
        return False
    except Exception as exc:  # noqa: BLE001
        print(f"  FAIL {url}  [{exc}]")
        return False
    with open(dest, "wb") as handle:
        handle.write(data)
    print(f"  OK   {url}")
    time.sleep(THROTTLE_SEC)
    return True


def fetch_layer(entry: dict, force: bool, dry_run: bool) -> None:
    layer = entry["layer"]
    style = entry["style"]
    path = entry["path"]
    mode = entry["mode"]

    if mode == "master":
        for upstream_anim, our_key in ANIMATIONS:
            url = f"{LPC_BASE}/{urllib.parse.quote(path + '/' + upstream_anim + '.png')}"
            dest = os.path.join(RAW_ROOT, layer, style, "master", f"{our_key}.png")
            download(url, dest, force, dry_run)
        return

    if mode == "colored":
        for our_color, upstream_color in entry["colors"]:
            for upstream_anim, our_key in ANIMATIONS:
                url = (
                    f"{LPC_BASE}/"
                    f"{urllib.parse.quote(path + '/' + upstream_anim + '/' + upstream_color + '.png')}"
                )
                dest = os.path.join(
                    RAW_ROOT, layer, style, our_color, f"{our_key}.png",
                )
                download(url, dest, force, dry_run)
        return

    raise ValueError(f"Unknown mode {mode!r} for layer {layer}/{style}")


def fetch_all_layers(entries: Iterable[dict], force: bool, dry_run: bool) -> None:
    for entry in entries:
        fetch_layer(entry, force, dry_run)


def fetch_palettes(force: bool, dry_run: bool) -> None:
    """Download palette definition JSON files for the build-time baker."""
    for rel_path in PALETTE_FILES:
        filename = os.path.basename(rel_path)
        url = f"{LPC_BASE}/{urllib.parse.quote(rel_path)}"
        dest = os.path.join(PALETTE_ROOT, filename)
        download(url, dest, force, dry_run)


def main() -> None:
    parser = argparse.ArgumentParser(description="Fetch LPC source art.")
    parser.add_argument("--force",   action="store_true",
                        help="redownload existing files")
    parser.add_argument("--dry-run", action="store_true",
                        help="list URLs without downloading")
    parser.add_argument("--only",    type=str, default=None,
                        help="fetch only this layer (body, head, hair, facial, torso, legs, feet, eyes, palettes)")
    args = parser.parse_args()

    print(f"Raw root: {RAW_ROOT}")

    if not args.only or args.only == "palettes":
        print("\nFetching palette definitions...")
        fetch_palettes(args.force, args.dry_run)

    filtered = ALL_LAYERS
    if args.only and args.only != "palettes":
        filtered = [e for e in ALL_LAYERS if e["layer"] == args.only]
        if not filtered:
            print(f"No layers match --only {args.only}")
            return

    print("\nFetching per-animation PNGs...")
    fetch_all_layers(filtered, args.force, args.dry_run)

    print("\nDone.")


if __name__ == "__main__":
    main()
