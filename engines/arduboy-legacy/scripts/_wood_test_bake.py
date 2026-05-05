#!/usr/bin/env python3
"""Bake the Wood test map: ground tilemap + object sprites + object list.

Two-layer composition matching engine/world_objects.h:
  - Ground tilemap (engine/tilemap.h scrolling renderer): a 16x8 grid
    of palette indices over a tiny ground-only palette (grass A/B,
    dirt path). Walkable everywhere.
  - Object layer (engine/world_objects.h): discrete sprites placed at
    arbitrary world coordinates — tree (solid), stone (interaction
    marker). Pre-sorted by Y for free Y-sort at render time.

Underscore-prefixed = throwaway scaffold for the Wood-as-hub bring-up.
The "real" Wood data will eventually come from a browser map editor
(planned) emitting JSON; this script is the manual stand-in until that
exists.

Outputs (all FX-flash-bound, registered in tools/fxdata/manifest.txt):
  data/fx/wood_test/palette.bin         — ground tile palette
  data/fx/wood_test/layout.bin          — ground tile layout
  data/fx/wood_test/object_sprites.bin  — concatenated object sprite bytes
  data/fx/wood_test/object_index.bin    — sprite_id -> (offset, w, h) records
  data/fx/wood_test/objects.bin         — world_objects::Object records

Usage: python scripts/_wood_test_bake.py
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT  = ROOT / "data" / "fx" / "wood_test"

TILE_W = 16
TILE_H = 16


def encode_tile(rows: list[str]) -> bytes:
    """rows: 16 strings of 16 chars; '.' = pixel off, anything else = on.
    Output: 32 bytes column-major, two pages stacked (rows 0..7 then 8..15).
    Matches engine/tilemap.h tile encoding."""
    assert len(rows) == 16 and all(len(r) == 16 for r in rows)
    out = bytearray(32)
    for page in range(2):
        for c in range(TILE_W):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                if rows[y][c] != ".":
                    byte |= 1 << bit
            out[page * TILE_W + c] = byte
    return bytes(out)


def encode_sprite(rows: list[str]) -> bytes:
    """Same column-major encoding as tiles, but width/height are
    arbitrary. Output bytes per sprite = width * ceil(height/8).
    Matches engine/framebuffer.h fb::draw_sprite expectations."""
    h = len(rows)
    w = len(rows[0]) if rows else 0
    assert all(len(r) == w for r in rows), "sprite rows must be uniform width"
    pages = (h + 7) // 8
    out = bytearray(w * pages)
    for page in range(pages):
        for c in range(w):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                if y < h and rows[y][c] != ".":
                    byte |= 1 << bit
            out[page * w + c] = byte
    return bytes(out), w, h


# --- Ground palette: 2 tiles. Tile 0 is implicit-empty per engine convention,
# so palette indices 1..2 = stored slots 0..2. Path is dropped — paths come
# back as object-layer sprites later if we want them.
#
# Aesthetic: the ground is supposed to be QUIET. The wood is folk-horror
# selva oscura, not a manicured Pokémon route. All visual interest belongs
# to the object layer (trees, NPCs, sites). The tiles below are deliberately
# bland — single-pixel sparse noise, ~10% lit, no recognizable pattern that
# would betray the tile boundary on tessellation. Adjacent tiles must not
# produce visible seams; ran the encoder against itself to confirm the noise
# distribution doesn't pile pixels at the cell edges.

# Tile 1: GROUND_A — sparse irregular dots. Asymmetric placement so a 2x2
# of this tile doesn't look like a regular grid. Roughly 12 pixels lit
# out of 256 (~5% density).
GROUND_A = encode_tile([
    "................",
    "....X...........",
    "................",
    "................",
    "..........X.....",
    "................",
    "...............X",
    "................",
    "X...............",
    "................",
    "......X.........",
    "................",
    "............X...",
    "................",
    "...X............",
    "................",
])

# Tile 2: GROUND_B — same density, completely different pixel placement.
# Used as scattered breaks in the GROUND_A field to disrupt any regular
# read. About 11 pixels lit.
GROUND_B = encode_tile([
    "................",
    "..............X.",
    "................",
    ".......X........",
    "................",
    "................",
    "..X.............",
    "................",
    "...........X....",
    "................",
    "................",
    ".....X..........",
    "................",
    ".X..............",
    "................",
    ".........X......",
])


def build_palette() -> bytes:
    return GROUND_A + GROUND_B


def build_layout() -> bytes:
    """16 wide × 8 tall ground grid. Mostly GROUND_A with occasional
    GROUND_B sprinkled in to break tessellation regularity. Sprinkle
    pattern is a hand-picked irregular set, NOT a checkerboard (which
    visibly reads as a pattern at the cell scale)."""
    W, H = 16, 8
    grid = [[1] * W for _ in range(H)]
    # Scatter GROUND_B at irregular positions. About 1 in 8 cells.
    sprinkle = {
        (1, 4), (2, 11), (3, 2), (3, 14),
        (5, 7), (6, 0), (6, 13), (7, 5), (7, 9),
        (0, 8), (4, 3),
    }
    for (r, c) in sprinkle:
        if 0 <= r < H and 0 <= c < W:
            grid[r][c] = 2
    return bytes(c for row in grid for c in row)


# --- Object sprites: tree (16w x 24h, solid) and stone (10w x 8h,
# interaction marker). Object-layer sprites can be any size.

TREE_ROWS = [
    "................",
    ".......XX.......",
    "......XXXX......",
    ".....XXXXXX.....",
    "....X.XXXX.X....",
    "...XXXXXXXXXX...",
    "..XX.XXXXXX.XX..",
    "..XXXXX..XXXXX..",
    ".XXXXXXXXXXXXXX.",
    ".XXX.XXXXXX.XXX.",
    ".XXXXXXXXXXXXXX.",
    "..XXXX.XXXX.XX..",
    "...XXXXXXXXXX...",
    "....XX.XX.XX....",
    ".....XXXXXX.....",
    "........X.......",
    ".......XXX......",
    ".......XXX......",
    ".......XXX......",
    ".......XXX......",
    ".......XXX......",
    "......XXXXX.....",
    ".....XXXXXXX....",
    "................",
]

# Stone: small lump, taller-than-wide reads as standing menhir/cairn
# rather than the UI-symbol cross-in-circle the placeholder had. ~10x12.
STONE_ROWS = [
    "..XXXX....",
    ".XXXXXX...",
    "XXXXXXXX..",
    "XXX..XXXX.",
    "XX....XXXX",
    "XX....XXXX",
    "XXX..XXXXX",
    "XXXXXXXXXX",
    "XXXXXXXXXX",
    ".XXXXXXXX.",
    "..XXXXXX..",
    "...XXXX...",
]


# Object sprite ids — local to this map, not the global sprites:: enum.
# The renderer dispatches via the on-FX object-index table, so adding/
# removing object types here doesn't ripple into game code.
OBJ_TREE  = 0
OBJ_STONE = 1


def build_object_sprites() -> tuple[bytes, list[tuple[int, int, int]]]:
    """Returns (concatenated sprite bytes, list of (byte_offset, w, h)
    indexed by object sprite id)."""
    blob = b""
    index: list[tuple[int, int, int]] = []
    for rows in (TREE_ROWS, STONE_ROWS):
        bytes_, w, h = encode_sprite(rows)
        index.append((len(blob), w, h))
        blob += bytes_
    return blob, index


def build_object_index(index: list[tuple[int, int, int]]) -> bytes:
    """Each record: u16 byte_offset (little-endian), u8 width, u8 height.
    4 bytes per record. Indexed directly by object sprite_id."""
    out = bytearray()
    for off, w, h in index:
        out += struct.pack("<HBB", off, w, h)
    return bytes(out)


# --- Object list: world-coord placements. Pre-sorted by y at the end.

# world_objects::FLAG_SOLID = 0x01, FLAG_INTERACT = 0x02. Interact id
# packs into bits 2-5; we use a few small ids for wood-menu options
# (0 = OFFERINGS, 1 = GRIMOIRE, etc. — to be wired up by the game side).
FLAG_SOLID    = 0x01
FLAG_INTERACT = 0x02


def make_obj(x: int, y: int, sprite_id: int, flags: int) -> tuple[int, int, int, int]:
    return (x, y, sprite_id, flags)


def build_objects() -> tuple[bytes, int]:
    """Pack world objects. Tree is solid; stone is an interaction marker.
    Output layout: i16 x, i16 y, u8 sprite_id, u8 flags = 6 B each.
    Pre-sorted by y for the renderer's free Y-sort."""
    objs: list[tuple[int, int, int, int]] = []

    # World is 16x8 tiles = 256x128 px. Trees are 16w x 24h. Placement
    # is loose — irregular Y so the perimeter doesn't read as a fence,
    # gaps you can walk through, scattered interior trees as landmarks.

    # Top-edge tree ring at varied y so the perimeter reads as forest
    # rather than a level boundary.
    top_trees = [(0, -4), (32, -2), (60, -6), (96, -4), (128, -2),
                 (160, -6), (192, -4), (220, -2)]
    for x, y in top_trees:
        objs.append(make_obj(x, y, OBJ_TREE, FLAG_SOLID))

    # Bottom-edge ring (trees stick up from below the world bottom).
    bot_y = 128 - 24 + 8  # peeks up from below; partly off-world
    bot_trees = [(0, bot_y), (40, bot_y - 4), (72, bot_y),
                 (104, bot_y - 2), (140, bot_y), (176, bot_y - 4),
                 (208, bot_y), (236, bot_y - 2)]
    for x, y in bot_trees:
        objs.append(make_obj(x, y, OBJ_TREE, FLAG_SOLID))

    # Left and right edges, sparser.
    side_y_left  = [22, 56, 88]
    side_y_right = [30, 64, 96]
    for y in side_y_left:
        objs.append(make_obj(-2, y, OBJ_TREE, FLAG_SOLID))
    for y in side_y_right:
        objs.append(make_obj(244, y, OBJ_TREE, FLAG_SOLID))

    # Interior landmarks. Avoid the upper-left clearing where the
    # player starts (around 32, 32) and the central path of travel.
    interior = [
        (110, 24), (180, 36),  # mid-band landmarks
        (60, 80),  (200, 90),  # lower-band
    ]
    for x, y in interior:
        objs.append(make_obj(x, y, OBJ_TREE, FLAG_SOLID))

    # Stones — interaction markers (eventual hub-menu sites). Tucked
    # into legible spots, not adjacent to trees.
    objs.append(make_obj(60,  44, OBJ_STONE, FLAG_INTERACT | (0 << 2)))
    objs.append(make_obj(150, 70, OBJ_STONE, FLAG_INTERACT | (1 << 2)))

    # Pre-sort by y for the renderer's Y-sort assumption.
    objs.sort(key=lambda o: (o[1], o[0]))

    out = bytearray()
    for x, y, sid, flags in objs:
        out += struct.pack("<hhBB", x, y, sid, flags)
    return bytes(out), len(objs)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)

    palette = build_palette()
    layout  = build_layout()
    obj_sprite_bytes, obj_index_records = build_object_sprites()
    obj_index = build_object_index(obj_index_records)
    objects, n_objects = build_objects()

    (OUT / "palette.bin").write_bytes(palette)
    (OUT / "layout.bin").write_bytes(layout)
    (OUT / "object_sprites.bin").write_bytes(obj_sprite_bytes)
    (OUT / "object_index.bin").write_bytes(obj_index)
    (OUT / "objects.bin").write_bytes(objects)

    print(f"wrote palette.bin        ({len(palette)} B, 2 ground tiles)")
    print(f"wrote layout.bin         ({len(layout)} B, 16x8 grid)")
    print(f"wrote object_sprites.bin ({len(obj_sprite_bytes)} B, "
          f"{len(obj_index_records)} sprites)")
    print(f"wrote object_index.bin   ({len(obj_index)} B)")
    print(f"wrote objects.bin        ({len(objects)} B, {n_objects} placements)")
    print()
    print("manifest entries (already added):")
    print("  WOOD_TEST_PALETTE       data/fx/wood_test/palette.bin")
    print("  WOOD_TEST_LAYOUT        data/fx/wood_test/layout.bin")
    print("  WOOD_TEST_OBJ_SPRITES   data/fx/wood_test/object_sprites.bin")
    print("  WOOD_TEST_OBJ_INDEX     data/fx/wood_test/object_index.bin")
    print("  WOOD_TEST_OBJECTS       data/fx/wood_test/objects.bin")
    print(f"  (object count constant: WOOD_TEST_OBJECT_COUNT = {n_objects})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
