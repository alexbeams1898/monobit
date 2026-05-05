// Tile-palette scene renderer. See tilemap.h for the storage scheme.

#include "tilemap.h"

#include "data_flash.h"
#include "framebuffer.h"
#include "progmem.h"

namespace tilemap {

void draw(const Scene& scene) {
  // Walk the 32-byte layout in row-major order. Each layout entry is an
  // index into the palette (0..N-1). Each tile is 16 wide × 2 pages of 8
  // tall — same multi-page layout the regular sprite blitter consumes.
  for (u8 r = 0; r < SCENE_H_TILES; ++r) {
    const i16 ty = (i16)r * (i16)TILE_H;
    for (u8 c = 0; c < SCENE_W_TILES; ++c) {
      const i16 tx        = (i16)c * (i16)TILE_W;
      const u8 layout_idx = (u8)(r * SCENE_W_TILES + c);
      const u8 tile_idx   = pgm_read_byte(&scene.layout[layout_idx]);
      // Tile 0 is the all-zeros "empty" tile by convention — skipped
      // here, never stored in the palette. The palette holds tiles 1..N
      // packed at offsets 0..(N-1)*TILE_BYTES, so the offset for
      // tile_idx becomes (tile_idx - 1) * TILE_BYTES. For GATE that's
      // 16 of 32 cells (50%) avoiding 32 B of zero blits each, plus
      // 32 B of flash saved on the palette itself.
      if (tile_idx == 0) continue;
      const u8* tile_data = scene.palette + (u16)(tile_idx - 1) * (u16)TILE_BYTES;
      // Top page (rows 0..7) and bottom page (rows 8..15) share width=16
      // and the same draw_sprite_progmem call, advanced by one page worth
      // of bytes (TILE_W = 16).
      fb::draw_sprite_progmem(tx, ty, TILE_W, tile_data);
      fb::draw_sprite_progmem(tx, ty + 8, TILE_W, tile_data + TILE_W);
    }
  }
}

void draw_fx(u32 palette_fx_offset, const u8* layout) {
  // FX-resident palette variant. Same algorithm as draw() above except
  // each tile's 32 bytes are read from FX flash into a stack buffer and
  // blitted via the RAM-source fb::draw_sprite. Layout stays in PROGMEM
  // (32 bytes — small, hot-ish in the sense that it's read every cell).
  u8 tile_buf[TILE_BYTES];
  for (u8 r = 0; r < SCENE_H_TILES; ++r) {
    const i16 ty = (i16)r * (i16)TILE_H;
    for (u8 c = 0; c < SCENE_W_TILES; ++c) {
      const i16 tx        = (i16)c * (i16)TILE_W;
      const u8 layout_idx = (u8)(r * SCENE_W_TILES + c);
      const u8 tile_idx   = pgm_read_byte(&layout[layout_idx]);
      // Tile 0 = empty (see draw() above for the full rationale).
      if (tile_idx == 0) continue;
      const u32 tile_off = palette_fx_offset + (u32)(tile_idx - 1) * (u32)TILE_BYTES;
      data_flash::read(tile_off, tile_buf, TILE_BYTES);
      // Top page (rows 0..7) and bottom page (rows 8..15) split the
      // 32-byte buffer; both blit via the RAM-source draw_sprite.
      fb::draw_sprite(tx, ty, TILE_W, tile_buf);
      fb::draw_sprite(tx, ty + 8, TILE_W, tile_buf + TILE_W);
    }
  }
}

}  // namespace tilemap
