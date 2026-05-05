#include "images.h"

#include "progmem.h"

namespace images {

// GATE_PALETTE_data migrated to FX flash 2026-04-26 — see
// tools/fxdata/manifest.txt's GATE_PALETTE entry and
// fxdata::OFFSET_GATE_PALETTE in the generated header. Net effect:
// 512 B PROGMEM freed. Read via tilemap::draw_fx() with a 32-byte
// stack buffer per tile (16 SPI reads per gate render, ~7 ms total —
// cold path during GATE_CARD transitions only). The 32-byte layout
// (GATE_LAYOUT_data below) stays in PROGMEM since draw_fx walks it
// once per cell.

const u8 GATE_LAYOUT_data[32] PROGMEM = {
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0E, 0x0F, 0x10, 0x00, 0x00,
};

// LZ77-compressed via scripts/_lz77_bake.py. Decode with lz77::decode().
// Compressed: 348 B vs raw 1024 B; saves 676 B of flash.
// PRE-MASKED: pixels under the SELVA OSCURA plaque (x=28..99, y=11..61)
// and the HUD strip (y=0..8) are zeroed at bake time. draw_main_menu
// overpaints those regions every frame anyway, so storing them costs
// flash for nothing. Long zero runs LZ77-compress to almost nothing.
// If the plaque geometry changes, re-bake via scripts/_mask_forest.py.
// FOREST_LZ77_data migrated to FX flash 2026-04-26 — see
// tools/fxdata/manifest.txt's FOREST_LZ77 entry. Net effect: 348 B
// PROGMEM freed. Decode path: lz77::decode_from_fx into fb::buffer
// (same path TITLE_LZ77 already uses; cold path, ~1 ms once-per-MENU).

}  // namespace images
