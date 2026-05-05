// Per-game full-screen images (1024 bytes each, page-major framebuffer
// layout). Stored in PROGMEM and copied to fb::buffer via
// `fb::draw_full_image()`.
//
// Source PNGs live in `art/` and are converted with
// `scripts/png_to_image.py`.

#pragma once

#include "progmem.h"
#include "types.h"

namespace images {

// TITLE_LZ77_data lives on the FX flash chip — see
// tools/fxdata/manifest.txt and the OFFSET_TITLE_LZ77 constant in the
// generated data_offsets.h. Streamed via lz77::decode_from_fx() at
// title-render time. Was 929 B PROGMEM internally before migration.
//
// FOREST under MAIN_MENU: LZ77-compressed bytes live on FX flash —
// see tools/fxdata/manifest.txt's FOREST_LZ77 entry and
// fxdata::OFFSET_FOREST_LZ77 in the generated header. Streamed via
// lz77::decode_from_fx() at draw_main_menu time. Pre-masked at bake
// time so pixels under the SELVA OSCURA plaque (x=28..99, y=11..61)
// and HUD strip (y=0..8) are zeroed — those regions get overpainted
// every frame anyway, and zero runs LZ77 to almost nothing. Re-bake
// via scripts/_mask_forest.py if plaque geometry shifts.
// GATE is tile-encoded (see engine/tilemap.h). Palette migrated to FX
// flash 2026-04-26 (was 512 B PROGMEM); layout stays in PROGMEM (32 B).
// 16 unique 16×16 tiles in the palette. Tile 0 (all-black sky) is by
// convention "empty" — the tilemap renderer skips layout cells with
// index 0 instead of blitting zero bytes, so tile 0 isn't stored at
// all. Layout indices 1..16 map to palette offsets 0..15 × 32 B
// (renderer subtracts 1). Read via tilemap::draw_fx() with
// fxdata::OFFSET_GATE_PALETTE.
extern const u8 GATE_LAYOUT_data[32] PROGMEM;  // tile indices, row-major 8x4

}  // namespace images
