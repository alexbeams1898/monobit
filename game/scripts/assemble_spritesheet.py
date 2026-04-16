"""
Assemble LPC per-animation PNGs into the engine sprite sheet layout.

Input tree (populated by fetch_lpc.py + bake_palettes.py):
  game/assets/sprites/lpc/raw/<layer>/<style>/<color>/<anim>.png

Per-animation PNG layout (native LPC):
  Rows    = directions in order Up(0), Left(1), Down(2), Right(3)
  Columns = animation frames
  Frame   = 64x64 pixels
  walk.png  is 576x256 (9 frames)
  run.png   is 512x256 (8 frames)
  slash.png is 384x256 (6 frames)
  hurt.png  is 384x64  (6 frames, south only -- broadcast to all dirs)

Output sheet layout (engine format, 3328x576):
  Rows    = states: idle(0), walk(1), slash(2), hit(3), death(4), run(5),
            thrust(6), shoot(7), reverse_slash(8)
  Columns = direction blocks, each 13 frames wide (max frame count = shoot = 13)
  Column  = dir_index * 13 + frame_index
  Direction order: South(0), West(1), East(2), North(3)

Output path convention (read by layers.json + AppearanceOps):
  assets/sprites/lpc/assembled/<layer>/<style>_<color>.png
  Body/head/eyes use just the color:
  assets/sprites/lpc/assembled/body/<color>.png
  assets/sprites/lpc/assembled/head/<color>_<variant>.png
  assets/sprites/lpc/assembled/eyes/<color>.png

Usage:
  python scripts/assemble_spritesheet.py            # assemble everything
  python scripts/assemble_spritesheet.py --force    # overwrite existing
  python scripts/assemble_spritesheet.py --layer hair
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is required. Install with `pip install Pillow`.")
    sys.exit(1)


# --------------------------------------------------------------------------- #
# Constants
# --------------------------------------------------------------------------- #

FRAME_SIZE = 64
NUM_DIRS = 4
NUM_STATES = 9
MAX_FRAMES_PER_STATE = 13  # shoot/reverse_slash have 13 frames

SHEET_WIDTH  = NUM_DIRS * MAX_FRAMES_PER_STATE * FRAME_SIZE  # 3328
SHEET_HEIGHT = NUM_STATES * FRAME_SIZE                        # 576

# LPC row (direction) -> our direction index in the output sheet.
# LPC order: Up=0, Left=1, Down=2, Right=3
# Our order: South=0, West=1, East=2, North=3
LPC_ROW_TO_OUR_DIR = {
    2: 0,  # Down  -> South
    1: 1,  # Left  -> West
    3: 2,  # Right -> East
    0: 3,  # Up    -> North
}

# Engine state row definitions.
# (our_row, anim_file_key, max_frames_to_copy, broadcast_to_all_dirs, start_frame)
# - broadcast: hurt.png is 1 direction (south); copy it to all 4 dirs.
# - idle uses the first frame of walk (which in LPC is the rest/stand pose).
# - walk skips LPC frame 0 (the rest pose) and uses frames 1..8 as the actual
#   walking cycle; otherwise playback loops through rest every ~640ms which
#   looks like the character briefly returning to a standing pose each cycle.
STATE_ROWS = [
    (0, "walk",          1,  False, 0),  # idle: LPC walk frame 0 (the rest pose)
    (1, "walk",          8,  False, 1),  # walk: LPC frames 1..8 (actual cycle)
    (2, "slash",         6,  False, 0),  # attack (melee)
    (3, "hurt",          6,  True,  0),  # hit
    (4, "hurt",          6,  True,  0),  # death (reuse hurt)
    (5, "run",           8,  False, 0),  # run (sprint)
    (6, "thrust",        8,  False, 0),  # thrust (two-handed aim pose)
    (7, "shoot",         13, False, 0),  # shoot (one-handed ranged)
    (8, "reverse_slash", 13, False, 0),  # reverse slash
]


# --------------------------------------------------------------------------- #
# Paths
# --------------------------------------------------------------------------- #

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GAME_DIR   = os.path.dirname(SCRIPT_DIR)
RAW_ROOT       = os.path.join(GAME_DIR, "assets", "sprites", "lpc", "raw")
ASSEMBLED_ROOT = os.path.join(GAME_DIR, "assets", "sprites", "lpc", "assembled")


# --------------------------------------------------------------------------- #
# Assembly manifest
# --------------------------------------------------------------------------- #
#
# Each target sheet comes from a (layer, style, color) tuple under raw/.
# The manifest lists which combinations to emit and where to save them.

SKIN_TONES = [
    "tone_1", "tone_2", "tone_3", "tone_4", "tone_5", "tone_6", "tone_7",
    "tone_8", "tone_9", "tone_10", "tone_11", "tone_12",
]

HAIR_COLORS = [
    "black", "charcoal", "gray", "brown", "ash_brown", "chestnut",
    "blonde", "red", "orange", "pink", "blue", "green",
]

EYE_COLORS = ["brown", "blue", "green", "gray"]

FEET_COLORS = [
    "white", "black", "gray", "brown", "tan",
    "red", "orange", "yellow", "green", "blue", "purple", "pink",
]

CLOTH_COLORS = FEET_COLORS  # same vocabulary for torso/legs

HAIR_STYLES    = [
    "plain", "buzzcut", "spiked", "messy", "long", "shorthawk",
    "flat_top", "parted", "cowlick", "page", "curly", "idol",
    "bangs_bun", "cornrows", "dreadlocks", "bob_side", "bob_bangs",
]

FACIAL_STYLES  = [
    "basic", "shadow", "winter", "trimmed", "medium",
    "walrus", "chevron", "handlebar", "bigstache", "french",
    "horseshoe", "lampshade",
]

HEAD_VARIANTS  = ["base", "elderly", "gaunt", "plump", "small"]

HEADWEAR_STYLES = ["bowler", "tophat"]
HEADWEAR_COLORS = [
    "black", "gray", "brown", "tan", "white",
    "red", "blue", "green", "navy", "purple",
]


def build_manifest() -> list:
    """Return list of (source_dir, output_path) pairs to assemble."""
    jobs = []

    # Body -> assembled/body/<tone>.png
    for tone in SKIN_TONES:
        jobs.append((
            os.path.join(RAW_ROOT, "body", "male", tone),
            os.path.join(ASSEMBLED_ROOT, "body", f"{tone}.png"),
        ))

    # Skeleton body -> assembled/body/skeleton.png
    jobs.append((
        os.path.join(RAW_ROOT, "body", "skeleton", "skeleton"),
        os.path.join(ASSEMBLED_ROOT, "body", "skeleton.png"),
    ))

    # Heads -> assembled/head/<tone>_<variant>.png
    # Order matches AppearanceOps combine_with: "{other_selection}_{this_selection}.png"
    # where other = body_color (tone) and this = head_variant.
    for variant in HEAD_VARIANTS:
        for tone in SKIN_TONES:
            jobs.append((
                os.path.join(RAW_ROOT, "head", variant, tone),
                os.path.join(ASSEMBLED_ROOT, "head", f"{tone}_{variant}.png"),
            ))

    # Skeleton head -> assembled/head/skeleton_skeleton.png
    jobs.append((
        os.path.join(RAW_ROOT, "head", "skeleton", "skeleton"),
        os.path.join(ASSEMBLED_ROOT, "head", "skeleton_skeleton.png"),
    ))

    # Eyes -> assembled/eyes/<color>.png
    for color in EYE_COLORS:
        jobs.append((
            os.path.join(RAW_ROOT, "eyes", "eyes", color),
            os.path.join(ASSEMBLED_ROOT, "eyes", f"{color}.png"),
        ))

    # Hair -> assembled/hair/<style>_<color>.png
    for style in HAIR_STYLES:
        for color in HAIR_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "hair", style, color),
                os.path.join(ASSEMBLED_ROOT, "hair", f"{style}_{color}.png"),
            ))

    # Facial hair -> assembled/facial/<style>_<color>.png
    for style in FACIAL_STYLES:
        for color in HAIR_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "facial", style, color),
                os.path.join(ASSEMBLED_ROOT, "facial", f"{style}_{color}.png"),
            ))

    # Torso -> assembled/torso/<style>_<color>.png
    for style in ("shortsleeve", "longsleeve"):
        for color in CLOTH_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "torso", style, color),
                os.path.join(ASSEMBLED_ROOT, "torso", f"{style}_{color}.png"),
            ))

    # Legs -> assembled/legs/<style>_<color>.png
    for style in ("pants", "shorts"):
        for color in CLOTH_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "legs", style, color),
                os.path.join(ASSEMBLED_ROOT, "legs", f"{style}_{color}.png"),
            ))

    # Feet -> assembled/feet/<style>_<color>.png
    for style in ("shoes", "boots"):
        for color in FEET_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "feet", style, color),
                os.path.join(ASSEMBLED_ROOT, "feet", f"{style}_{color}.png"),
            ))

    # Headwear -> assembled/headwear/<style>_<color>.png
    for style in HEADWEAR_STYLES:
        for color in HEADWEAR_COLORS:
            jobs.append((
                os.path.join(RAW_ROOT, "headwear", style, color),
                os.path.join(ASSEMBLED_ROOT, "headwear", f"{style}_{color}.png"),
            ))

    return jobs


# --------------------------------------------------------------------------- #
# Assembly logic
# --------------------------------------------------------------------------- #


def load_animation(src_dir: str, anim_key: str) -> Image.Image | None:
    """Load an animation PNG, converting palette-indexed to RGBA."""
    path = os.path.join(src_dir, f"{anim_key}.png")
    if not os.path.exists(path):
        return None
    return Image.open(path).convert("RGBA")


def copy_frame(src: Image.Image, sx: int, sy: int,
               dst: Image.Image, dx: int, dy: int) -> None:
    """Copy a single 64x64 frame from src at (sx,sy) to dst at (dx,dy)."""
    frame = src.crop((sx, sy, sx + FRAME_SIZE, sy + FRAME_SIZE))
    dst.alpha_composite(frame, dest=(dx, dy))


def assemble_sheet(src_dir: str) -> Image.Image:
    """Assemble one 2048x384 sheet from per-animation PNGs in src_dir."""
    sheet = Image.new("RGBA", (SHEET_WIDTH, SHEET_HEIGHT), (0, 0, 0, 0))

    for our_row, anim_key, max_frames, broadcast, start_frame in STATE_ROWS:
        anim = load_animation(src_dir, anim_key)
        if anim is None:
            continue

        aw, _ah = anim.size
        src_cols = aw // FRAME_SIZE
        frame_count = min(max_frames, src_cols - start_frame)

        for lpc_row, our_dir in LPC_ROW_TO_OUR_DIR.items():
            sy = 0 if broadcast else lpc_row * FRAME_SIZE
            for frame in range(frame_count):
                sx = (start_frame + frame) * FRAME_SIZE
                dx = (our_dir * MAX_FRAMES_PER_STATE + frame) * FRAME_SIZE
                dy = our_row * FRAME_SIZE
                copy_frame(anim, sx, sy, sheet, dx, dy)

    return sheet


def run_job(src_dir: str, out_path: str, force: bool) -> tuple[bool, str]:
    """Assemble one sheet. Returns (success, status_msg)."""
    if not os.path.isdir(src_dir):
        return False, f"MISS src dir: {src_dir}"
    if os.path.exists(out_path) and not force:
        return True, f"SKIP {out_path}"

    sheet = assemble_sheet(src_dir)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    sheet.save(out_path)
    return True, f"OK   {out_path}"


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #


def main() -> None:
    parser = argparse.ArgumentParser(description="Assemble LPC sprite sheets.")
    parser.add_argument("--force", action="store_true", help="overwrite existing sheets")
    parser.add_argument("--layer", type=str, default=None,
                        help="only assemble for a layer name (body, head, hair, etc.)")
    args = parser.parse_args()

    jobs = build_manifest()
    if args.layer:
        jobs = [j for j in jobs if f"{os.sep}{args.layer}{os.sep}" in j[0]]

    print(f"Assembling {len(jobs)} sheets...")
    built = 0
    skipped = 0
    missed = 0
    for src, dst in jobs:
        ok, msg = run_job(src, dst, args.force)
        if not ok:
            print(f"  {msg}")
            missed += 1
        elif msg.startswith("SKIP"):
            skipped += 1
        else:
            built += 1
    print(f"\nBuilt {built}, skipped {skipped}, missed {missed}.")


if __name__ == "__main__":
    main()
