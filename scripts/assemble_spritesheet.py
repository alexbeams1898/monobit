"""
Assemble LPC (Liberated Pixel Cup) character layers into sprite sheets.

LPC sprites are composited from multiple transparent layers (body, head, eyes,
torso, legs, feet, hair, weapon) -- each layer is a separate PNG per animation.
This script composites them into single sprite sheets matching our animation
system's row/column layout.

LPC source layout (per animation PNG):
  Rows = directions: Up(0), Left(1), Down(2), Right(3)
  Cols = animation frames
  Frame size: 64x64

Our sheet layout:
  Rows = states: Idle(0), Walk(1), Attack(2), Hit(3), Death(4)
  Cols = direction blocks x max_frames_per_state
  Direction order: South(0), West(1), East(2), North(3)
  Col = dir_index * max_frames + frame_index

Modes:
  --split: output separate lower-body and upper-body sheets for the player
           (for split-body rendering: legs face velocity, torso faces aim)

License: LPC assets are dual-licensed GPL 3.0 / CC-BY-SA 3.0.
"""

import os
import sys
from PIL import Image

FRAME_SIZE = 64  # native LPC frame size
NUM_DIRS = 4
NUM_STATES = 5

# LPC direction row -> our direction index
# LPC: Up=0, Left=1, Down=2, Right=3
# Ours: South=0, West=1, East=2, North=3
LPC_ROW_TO_DIR = {
    2: 0,  # Down -> South
    1: 1,  # Left -> West
    3: 2,  # Right -> East
    0: 3,  # Up -> North
}

# Animation states: (our_row, lpc_anim_name, max_frames_to_use)
# LPC frame counts: idle=2, walk=9, slash=6, hurt=6(south only)
ANIM_STATES = [
    (0, "idle", 2),
    (1, "walk", 9),
    (2, "slash", 6),    # attack
    (3, "hurt", 1),     # only south has frames; other dirs use south
    (4, "hurt", 2),     # death: reuse hurt frames
]

# Layer groups for split-body rendering.
# Head-only split: upper body is just the head (faces aim direction),
# lower body is everything else (arms, torso, legs -- faces movement).
# This avoids arm artifacts from the torso split and looks clean with
# placeholder LPC art. Custom torso-twist art is planned for later.
LOWER_BODY_LAYERS = ["", "torso", "legs", "feet"]
UPPER_BODY_LAYERS = ["", "head", "eyes", "hair"]

# Vertical cutoff line within each 64x64 frame. Rows above this belong
# to the upper body; rows at or below belong to the lower body.
# LPC head layer: y=15-35. Body base starts at y=32. Split at 36 captures
# the full head+chin without cutting through the face.
BODY_SPLIT_Y = 36

# All layers composited together (used for full-body sheets).
ALL_LAYERS = ["", "head", "eyes", "legs", "feet", "torso", "hair"]

# Universal LPC sheet layout (13 cols x 21 rows at 64x64).
# Maps animation blocks to their starting row in the universal sheet.
# Each block has 4 rows: Up(+0), Left(+1), Down(+2), Right(+3).
UNIVERSAL_ANIM_ROWS = {
    "spellcast": 0,   # 7 frames
    "thrust": 4,      # 8 frames
    "walk": 8,        # 9 frames
    "slash": 12,      # 6 frames
    "shoot": 16,      # 13 frames
    "hurt": 20,       # 6 frames, south only
}

# Direction mapping within universal sheet: row offset -> our direction index.
# Universal: Up=+0, Left=+1, Down=+2, Right=+3
# Ours: South=0, West=1, East=2, North=3
UNIVERSAL_DIR_OFFSET = {
    2: 0,  # Down -> South
    1: 1,  # Left -> West
    3: 2,  # Right -> East
    0: 3,  # Up -> North
}

# Which universal animations map to our game states, and how many frames to use.
# (our_row, universal_anim_name, frame_limit)
UNIVERSAL_STATE_MAP = [
    (0, "walk", 2),     # Idle: first 2 walk frames (standing)
    (1, "walk", 9),     # Walk: full walk cycle
    (2, "slash", 6),    # Attack: slash animation
    (3, "hurt", 1),     # Hit: first hurt frame
    (4, "hurt", 2),     # Death: first 2 hurt frames
]


def composite_layers(layer_paths):
    """Alpha-composite multiple layer PNGs into a single image."""
    result = None
    for path in layer_paths:
        if not os.path.exists(path) or os.path.getsize(path) < 100:
            continue
        try:
            layer = Image.open(path).convert("RGBA")
        except Exception:
            continue

        if result is None:
            result = Image.new("RGBA", layer.size, (0, 0, 0, 0))

        if layer.size != result.size:
            layer = layer.resize(result.size, Image.LANCZOS)
        result = Image.alpha_composite(result, layer)
    return result


def extract_frames(sheet, frame_count):
    """Extract 64x64 frames from an LPC animation sheet.

    Returns dict: {lpc_row: [frame0, frame1, ...]}
    """
    if sheet is None:
        return {}

    w, h = sheet.size
    cols = w // FRAME_SIZE
    rows = h // FRAME_SIZE
    actual_frames = min(cols, frame_count)

    frames = {}
    for row in range(rows):
        row_frames = []
        for col in range(actual_frames):
            x = col * FRAME_SIZE
            y = row * FRAME_SIZE
            row_frames.append(sheet.crop((x, y, x + FRAME_SIZE, y + FRAME_SIZE)))
        frames[row] = row_frames
    return frames


def mask_sheet_vertical(sheet, keep_bottom):
    """Zero out alpha in the top or bottom half of every frame in the sheet.

    keep_bottom=True  -> lower body: keep rows >= BODY_SPLIT_Y, clear above
    keep_bottom=False -> upper body: keep rows <  BODY_SPLIT_Y, clear below
    """
    pixels = sheet.load()
    for py in range(sheet.height):
        local_y = py % FRAME_SIZE
        should_clear = (local_y < BODY_SPLIT_Y) if keep_bottom else (local_y >= BODY_SPLIT_Y)
        if should_clear:
            for px in range(sheet.width):
                r, g, b, a = pixels[px, py]
                if a > 0:
                    pixels[px, py] = (r, g, b, 0)


def assemble_from_universal_sheet(sheet_path, output_path, tint=None):
    """Build a sprite sheet from a universal LPC sheet (single pre-composited PNG).

    Universal sheets have all animations in one file (832x1344, 13 cols x 21 rows).
    Extracts walk/slash/hurt and remaps to our 5-row game format.
    """
    if not os.path.exists(sheet_path):
        print(f"  ERROR: Universal sheet not found: {sheet_path}")
        return 0

    universal = Image.open(sheet_path).convert("RGBA")
    u_cols = universal.size[0] // FRAME_SIZE

    # Find max frames across all states we need
    max_frames = 0
    for _, anim_name, frame_limit in UNIVERSAL_STATE_MAP:
        base_row = UNIVERSAL_ANIM_ROWS[anim_name]
        # Check actual frame count from first direction row
        actual = u_cols
        max_frames = max(max_frames, min(actual, frame_limit))

    if max_frames == 0:
        print(f"  ERROR: No frames found in {sheet_path}")
        return 0

    sheet_w = NUM_DIRS * max_frames * FRAME_SIZE
    sheet_h = NUM_STATES * FRAME_SIZE
    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))

    for our_row, anim_name, frame_limit in UNIVERSAL_STATE_MAP:
        base_row = UNIVERSAL_ANIM_ROWS[anim_name]

        for dir_offset, our_dir in UNIVERSAL_DIR_OFFSET.items():
            src_row = base_row + dir_offset

            # hurt (row 20) only has south; reuse south for all dirs
            if anim_name == "hurt" and dir_offset != 2:
                src_row = base_row + 2  # down/south row

            # Check if src_row exists in the sheet
            if (src_row + 1) * FRAME_SIZE > universal.size[1]:
                # Row doesn't exist, try south fallback
                src_row = base_row + 2
                if (src_row + 1) * FRAME_SIZE > universal.size[1]:
                    continue

            for frame_idx in range(frame_limit):
                src_x = frame_idx * FRAME_SIZE
                src_y = src_row * FRAME_SIZE
                if src_x + FRAME_SIZE > universal.size[0]:
                    break

                frame = universal.crop((src_x, src_y, src_x + FRAME_SIZE, src_y + FRAME_SIZE))

                dst_col = our_dir * max_frames + frame_idx
                dst_x = dst_col * FRAME_SIZE
                dst_y = our_row * FRAME_SIZE
                sheet.paste(frame, (dst_x, dst_y), frame)

    if tint:
        r_tint, g_tint, b_tint = tint
        pixels = sheet.load()
        for py in range(sheet.height):
            for px in range(sheet.width):
                r, g, b, a = pixels[px, py]
                if a > 0:
                    pixels[px, py] = (
                        min(255, int(r * r_tint)),
                        min(255, int(g * g_tint)),
                        min(255, int(b * b_tint)),
                        a,
                    )

    sheet.save(output_path)
    print(f"  Saved {output_path} ({sheet.width}x{sheet.height})")
    return max_frames


def assemble_lpc_sheet(lpc_dir, output_path, layer_names, tint=None, vertical_mask=None):
    """Build a sprite sheet from composited LPC layers.

    layer_names: list of layer name prefixes to composite.
    vertical_mask: None (no mask), "lower" (keep bottom half), "upper" (keep top half)
    """
    # Composite layers for each animation
    anim_composites = {}
    for _, anim_name, _ in ANIM_STATES:
        if anim_name in anim_composites:
            continue
        paths = []
        for name in layer_names:
            if name:
                paths.append(os.path.join(lpc_dir, f"{name}_{anim_name}.png"))
            else:
                paths.append(os.path.join(lpc_dir, f"{anim_name}.png"))
        anim_composites[anim_name] = composite_layers(paths)

    # Determine max_frames across all states
    max_frames = 0
    for _, anim_name, frame_limit in ANIM_STATES:
        comp = anim_composites.get(anim_name)
        if comp:
            actual = comp.size[0] // FRAME_SIZE
            max_frames = max(max_frames, min(actual, frame_limit))

    if max_frames == 0:
        print(f"  ERROR: No valid composites found in {lpc_dir}")
        return 0

    # Build the sheet
    sheet_w = NUM_DIRS * max_frames * FRAME_SIZE
    sheet_h = NUM_STATES * FRAME_SIZE
    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))

    for our_row, anim_name, frame_limit in ANIM_STATES:
        comp = anim_composites.get(anim_name)
        frames_by_lpc_row = extract_frames(comp, frame_limit)

        for lpc_row, our_dir in LPC_ROW_TO_DIR.items():
            frames = frames_by_lpc_row.get(lpc_row, [])

            # hurt only has south (lpc_row 2); reuse south for other dirs
            if not frames and frames_by_lpc_row:
                frames = frames_by_lpc_row.get(2, [])  # fallback to south
            if not frames:
                continue

            for frame_idx in range(min(len(frames), frame_limit)):
                col = our_dir * max_frames + frame_idx
                x = col * FRAME_SIZE
                y = our_row * FRAME_SIZE
                sheet.paste(frames[frame_idx], (x, y), frames[frame_idx])

    # Apply vertical mask for split-body sheets
    if vertical_mask == "lower":
        mask_sheet_vertical(sheet, keep_bottom=True)
    elif vertical_mask == "upper":
        mask_sheet_vertical(sheet, keep_bottom=False)

    # Apply color tint for enemy variant
    if tint:
        r_tint, g_tint, b_tint = tint
        pixels = sheet.load()
        for py in range(sheet.height):
            for px in range(sheet.width):
                r, g, b, a = pixels[px, py]
                if a > 0:
                    pixels[px, py] = (
                        min(255, int(r * r_tint)),
                        min(255, int(g * g_tint)),
                        min(255, int(b * b_tint)),
                        a,
                    )

    sheet.save(output_path)
    print(f"  Saved {output_path} ({sheet.width}x{sheet.height})")
    return max_frames


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    lpc_dir = os.path.join(os.environ.get("TEMP", "/tmp"), "lpc")

    sprites_dir = os.path.join(project_dir, "assets", "sprites")
    os.makedirs(sprites_dir, exist_ok=True)

    split_mode = "--split" in sys.argv
    skip_enemy = "--no-enemy" in sys.argv

    # Skeleton universal sheet for enemy. Look in $TEMP or /tmp.
    skeleton_path = os.path.join(os.environ.get("TEMP", "/tmp"), "lpc_skeleton_universal.png")

    if split_mode:
        print(f"Assembling player LOWER body sheet (body+legs+feet, masked below y={BODY_SPLIT_Y})...")
        max_f = assemble_lpc_sheet(
            lpc_dir,
            os.path.join(sprites_dir, "player_lower.png"),
            LOWER_BODY_LAYERS,
            vertical_mask="lower",
        )
        print(f"  max_frames_per_state = {max_f}")

        print(f"Assembling player UPPER body sheet (body+torso+head+eyes+hair, masked above y={BODY_SPLIT_Y})...")
        assemble_lpc_sheet(
            lpc_dir,
            os.path.join(sprites_dir, "player_upper.png"),
            UPPER_BODY_LAYERS,
            vertical_mask="upper",
        )
    else:
        print("Assembling player sprite sheet (LPC)...")
        max_f = assemble_lpc_sheet(
            lpc_dir,
            os.path.join(sprites_dir, "player_sheet.png"),
            ALL_LAYERS,
        )
        print(f"  max_frames_per_state = {max_f}")

    if not skip_enemy:
        if os.path.exists(skeleton_path):
            print("Assembling enemy sprite sheet (skeleton)...")
            assemble_from_universal_sheet(
                skeleton_path,
                os.path.join(sprites_dir, "enemy_sheet.png"),
            )
        else:
            print(f"  WARNING: Skeleton sheet not found at {skeleton_path}")
            print("  Falling back to red-tinted humanoid for enemy sheet.")
            assemble_lpc_sheet(
                lpc_dir,
                os.path.join(sprites_dir, "enemy_sheet.png"),
                ALL_LAYERS,
                tint=(1.0, 0.4, 0.4),
            )

    print("Done!")
    print(f"\nUpdate animation JSONs: frame_width={FRAME_SIZE}, frame_height={FRAME_SIZE}")
    if split_mode:
        print("Split sheets generated: player_lower.png, player_upper.png")


if __name__ == "__main__":
    main()
