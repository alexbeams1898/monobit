#!/usr/bin/env python3
"""Import a Universal-LPC-Spritesheet-Generator sheet into wayworn's animation
layout.

The LPC universal sheet is 64x64 frames, 13 columns, ~54 rows, one 4-direction
block per animation in row order N, W, S, E. This extracts the animations the
game uses (walk, run; idle = walk frame 0) and rewrites them into the engine's
sprite-sheet layout:

  column = dir_index * MAX_FRAMES + frame_index,   rows = states
  engine direction order: South=0, West=1, East=2, North=3

Re-run this whenever you regenerate the character in the LPC web tool.
"""

import sys

from PIL import Image

FRAME = 64

# LPC block rows are ordered N, W, S, E. Map LPC dir -> engine dir index.
# engine: S=0, W=1, E=2, N=3.  LPC block offset: N=0, W=1, S=2, E=3.
LPC_OFFSET_TO_ENGINE_DIR = {0: 3, 1: 1, 2: 0, 3: 2}  # N->3, W->1, S->0, E->2

# Animations to extract: (state_name, lpc_first_row, frame_count).
# Verified by eye against the generated sheet (see docs/design/SPRITE-PIPELINE).
WALK_ROW = 8   # rows 8-11 (N,W,S,E), 9 frames
RUN_ROW = 38   # rows 38-41, 8 frames
WALK_FRAMES = 9
RUN_FRAMES = 8

# Engine sheet: rows = states. Keep in sync with config/player.json.
STATE_ROW = {"walk": 0, "idle": 1, "run": 2}
MAX_FRAMES = 9  # widest state (walk); column stride


def extract_block(sheet, first_row, frames):
    """Return {engine_dir: [PIL frame, ...]} for one animation block."""
    out = {}
    for lpc_off, engine_dir in LPC_OFFSET_TO_ENGINE_DIR.items():
        row = first_row + lpc_off
        out[engine_dir] = [
            sheet.crop((c * FRAME, row * FRAME, c * FRAME + FRAME, row * FRAME + FRAME))
            for c in range(frames)
        ]
    return out


def paste_state(dest, row, block, frames):
    for engine_dir, cells in block.items():
        for f in range(frames):
            col = engine_dir * MAX_FRAMES + f
            dest.paste(cells[f], (col * FRAME, row * FRAME), cells[f])


def main(src, dst):
    sheet = Image.open(src).convert("RGBA")
    walk = extract_block(sheet, WALK_ROW, WALK_FRAMES)
    run = extract_block(sheet, RUN_ROW, RUN_FRAMES)

    rows = max(STATE_ROW.values()) + 1
    out = Image.new("RGBA", (4 * MAX_FRAMES * FRAME, rows * FRAME), (0, 0, 0, 0))

    paste_state(out, STATE_ROW["walk"], walk, WALK_FRAMES)
    paste_state(out, STATE_ROW["run"], run, RUN_FRAMES)
    # Idle = walk frame 0 held, one frame per direction.
    idle = {d: [cells[0]] for d, cells in walk.items()}
    paste_state(out, STATE_ROW["idle"], idle, 1)

    out.save(dst)
    print(f"wrote {dst} ({out.size[0]}x{out.size[1]}, {FRAME}px cells)")
    print(f"  walk row {STATE_ROW['walk']} ({WALK_FRAMES}f), "
          f"idle row {STATE_ROW['idle']} (1f), run row {STATE_ROW['run']} ({RUN_FRAMES}f)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: import_lpc.py <lpc_sheet.png> <out_player_walk.png>", file=sys.stderr)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
