// Tile-palette background renderer. Stores backgrounds as a small palette
// of unique 16x16 tiles plus per-scene layout tables (one byte per tile
// slot, indexing into the palette).
//
// Whole 128x64 framebuffer = 8 cols × 4 rows = 32 tile slots. A scene
// layout is therefore 32 bytes. The palette is N tiles × 32 bytes each
// (16 wide × 2 pages tall, column-major like every other sprite).
//
// Why this exists: full-screen 1-bit images cost 1024 B raw. Most of our
// backgrounds (forest, gates, etc.) are composed of a small visual
// vocabulary repeated across the canvas. A 16-tile palette + N layouts
// stores N backgrounds for ~512 + 32N bytes — three backgrounds shrink
// from 3072 B to ~608 B, and each additional one costs only 32 B.
//
// Trade-off: backgrounds become tile-composable, not pixel-arbitrary.
// Suits architectural / repeating-element scenes; would butcher a
// dithered photo. Mix freely with raw images::*_data for one-offs.

#pragma once

#include "types.h"

namespace tilemap {

// Each tile is 16x16 pixels stored column-major as 2 pages of 16 bytes
// each: bytes 0..15 are rows 0..7 of cols 0..15; bytes 16..31 are rows
// 8..15 of cols 0..15. Layout exactly mirrors how draw_progmem_sprite
// already consumes multi-page sprite data.
constexpr u8 TILE_W     = 16;
constexpr u8 TILE_H     = 16;
constexpr u8 TILE_BYTES = 32;  // TILE_W * (TILE_H / 8)

// Standard full-screen scene grid. Other sizes are possible (e.g. a
// half-screen panel that overlays gameplay) but every current caller is
// 8x4 tiles = 128x64 pixels.
constexpr u8 SCENE_W_TILES    = 8;
constexpr u8 SCENE_H_TILES    = 4;
constexpr u8 SCENE_LAYOUT_LEN = SCENE_W_TILES * SCENE_H_TILES;  // 32

// One renderable scene. Both pointers are PROGMEM addresses.
struct Scene {
  const u8* palette;  // PROGMEM, count×TILE_BYTES contiguous bytes
  const u8* layout;   // PROGMEM, SCENE_LAYOUT_LEN bytes of palette indices
};

// Draw a scene into the framebuffer. Walks the layout, blits each tile to
// its slot. Tile origin (0,0) lands at framebuffer (0,0); 8x4 layout
// covers the whole 128x64. Caller is responsible for any post-process
// (HUD strip clear, plaque overlay) AFTER this returns.
void draw(const Scene& scene);

// FX-resident palette variant. The palette lives on the Arduboy FX flash
// chip at byte offset `palette_fx_offset` (count×TILE_BYTES contiguous);
// the layout still lives in PROGMEM. Each non-empty tile costs one
// 32-byte SPI read into a stack-local buffer. Used for cold-path scenes
// (gate transitions etc.) where the per-tile read cost is negligible
// and the PROGMEM win is large (GATE_PALETTE alone = 512 B internal
// flash freed).
void draw_fx(u32 palette_fx_offset, const u8* layout);

}  // namespace tilemap
