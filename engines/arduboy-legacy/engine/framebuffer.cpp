#include "framebuffer.h"

#include "flash.h"
#include "perf_hook.h"
#include "progmem.h"

#include <string.h>

namespace fb {

u8 buffer[SIZE];

void clear() {
  perf_hook::charge(perf_hook::FB_CLEAR, 1);
  memset(buffer, 0, SIZE);
}

void invert_all() {
  perf_hook::charge(perf_hook::FB_INVERT_ALL, 1);
  for (u16 i = 0; i < SIZE; ++i)
    buffer[i] ^= 0xFF;
}

// 4x4 Bayer pattern — values 0..15 indicating "reveal order" within each
// 4x4 cell of the framebuffer. PROGMEM so it doesn't eat 16 B of RAM.
const u8 BAYER4[16] PROGMEM = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5,
};

u8 bayer_threshold(u8 x, u8 y) {
  return pgm_read_byte(&BAYER4[(y & 3) * 4 + (x & 3)]);
}

void dither_invert(u8 level) {
  if (level >= 16) return;  // nothing to do — full reveal
  if (level == 0) {         // every Bayer threshold is >= 0 -> invert all
    invert_all();
    return;
  }
  perf_hook::charge(perf_hook::FB_DITHER, 1);
  // Per-page row: the XOR mask depends only on the column's (x & 3) and the
  // row's (y & 3). Cache one mask per (column-mod-4, page) to avoid the
  // pgm_read in the hot inner loop. With 4 column mods and 8 pages the
  // cache is 32 bytes on the stack.
  u8 col_mask[4][8];
  for (u8 cx = 0; cx < 4; ++cx) {
    for (u8 page = 0; page < 8; ++page) {
      u8 m = 0;
      for (u8 b = 0; b < 8; ++b) {
        const u8 y     = (u8)(page * 8 + b);
        const u8 bayer = pgm_read_byte(&BAYER4[(y & 3) * 4 + cx]);
        // Invert pixels whose threshold is NOT yet revealed (>= level).
        if (bayer >= level) m |= (u8)(1u << b);
      }
      col_mask[cx][page] = m;
    }
  }
  for (u8 page = 0; page < 8; ++page) {
    u16 base = (u16)page * WIDTH;
    for (u8 x = 0; x < WIDTH; ++x) {
      buffer[base + x] ^= col_mask[x & 3][page];
    }
  }
}

void draw_full_image(const u8* flash_data) {
  flash::read(buffer, flash_data, SIZE);
}

void set_pixel(i16 x, i16 y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  // (y / 8) picks the page; (y & 7) picks the bit within that page's column byte.
  buffer[(u16)x + ((u16)(y >> 3) * WIDTH)] |= (u8)(1 << (y & 7));
}

void clear_pixel(i16 x, i16 y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  buffer[(u16)x + ((u16)(y >> 3) * WIDTH)] &= (u8) ~(1 << (y & 7));
}

void fill_rect(i16 x, i16 y, u8 w, u8 h) {
  for (u8 dy = 0; dy < h; ++dy) {
    for (u8 dx = 0; dx < w; ++dx) {
      set_pixel(x + (i16)dx, y + (i16)dy);
    }
  }
}

void clear_rect(i16 x, i16 y, u8 w, u8 h) {
  for (u8 dy = 0; dy < h; ++dy) {
    for (u8 dx = 0; dx < w; ++dx) {
      clear_pixel(x + (i16)dx, y + (i16)dy);
    }
  }
}

void stroke_rect(i16 x, i16 y, u8 w, u8 h) {
  if (w == 0 || h == 0) return;
  for (u8 dx = 0; dx < w; ++dx) {
    set_pixel(x + (i16)dx, y);
    set_pixel(x + (i16)dx, y + (i16)(h - 1));
  }
  for (u8 dy = 0; dy < h; ++dy) {
    set_pixel(x, y + (i16)dy);
    set_pixel(x + (i16)(w - 1), y + (i16)dy);
  }
}

// dup-ok: shared column-iteration framing across the four sprite-blit
// variants (draw/clear/outline × RAM/PROGMEM). Unifying via function-
// pointer adds an indirect call per column = ~1.5% CPU overhead in the
// hot draw path. Flash savings (~150 B) not worth the runtime cost.
void draw_sprite(i16 x, i16 y, u8 width, const u8* sprite) {
  perf_hook::charge(perf_hook::SPRITE_BLIT, width);
  // Vertical shift inside an 8-pixel page. If y is page-aligned (y % 8 == 0),
  // each source column byte lands in exactly one framebuffer byte. Otherwise
  // it straddles two pages and we have to write to both, with a shift.
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)col;
    if (px < 0 || px >= WIDTH) continue;

    const u8 src = sprite[col];

    // Upper page: shifted source bits land here.
    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] |= (u8)(src << shift);
    }
    // Lower page: only contributes if shift > 0 AND the page exists.
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] |= (u8)(src >> (8 - shift));
      }
    }
  }
}

// dup-ok: pgm_read_byte is a macro (lpm instruction on AVR) — cannot
// be passed as a function pointer or templatized cleanly. The cost of
// unifying with draw_sprite would be either 32-bit indirect calls per
// byte or a lambda specialization that LTO can't fold reliably. The
// math IS identical; only the source-byte fetch differs.
void draw_sprite_progmem(i16 x, i16 y, u8 width, const u8* flash_src) {
  perf_hook::charge(perf_hook::SPRITE_BLIT, width);
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)col;
    if (px < 0 || px >= WIDTH) continue;

    const u8 src = pgm_read_byte(&flash_src[col]);

    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] |= (u8)(src << shift);
    }
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] |= (u8)(src >> (8 - shift));
      }
    }
  }
}

// dup-ok: see draw_sprite_progmem. Same column-iteration framing but
// columns are walked in reverse — source col 0 lands at target col W-1,
// source col W-1 lands at target col 0. Per-byte data isn't bit-flipped
// (vertical bit order is the page byte itself, which stays top-up).
// Cheaper than maintaining a mirrored sprite copy; pairs with the
// player's "facing left" flag so animation strips drawn right-facing
// only need one direction's worth of frames in flash.
void draw_sprite_progmem_mirrored(i16 x, i16 y, u8 width, const u8* flash_src) {
  perf_hook::charge(perf_hook::SPRITE_BLIT, width);
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)((width - 1) - col);
    if (px < 0 || px >= WIDTH) continue;

    const u8 src = pgm_read_byte(&flash_src[col]);

    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] |= (u8)(src << shift);
    }
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] |= (u8)(src >> (8 - shift));
      }
    }
  }
}

// dup-ok: see draw_sprite_progmem. Same column-iteration framing, but
// the per-byte op is `&= ~src` (clear) instead of `|= src` (draw).
// Clear framebuffer pixels wherever the sprite is lit — the "black body"
// pass of the inverted-sprite scheme. Used on playfields with a mostly-lit
// background so foreground entities appear as cut-out silhouettes.
void clear_sprite_progmem(i16 x, i16 y, u8 width, const u8* flash_src) {
  perf_hook::charge(perf_hook::SPRITE_BLIT, width);
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)col;
    if (px < 0 || px >= WIDTH) continue;

    const u8 src = pgm_read_byte(&flash_src[col]);

    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] &= (u8) ~((u8)(src << shift));
    }
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] &= (u8) ~((u8)(src >> (8 - shift)));
      }
    }
  }
}

// dup-ok: same column-iteration framing as clear_sprite_progmem; only
// the per-byte source fetch differs (plain deref vs pgm_read_byte).
// Used by draw_shades_portrait to punch out boss silhouettes from a
// LZ77-decoded RAM cache.
void clear_sprite(i16 x, i16 y, u8 width, const u8* sprite) {
  perf_hook::charge(perf_hook::SPRITE_BLIT, width);
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)col;
    if (px < 0 || px >= WIDTH) continue;

    const u8 src = sprite[col];

    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] &= (u8) ~((u8)(src << shift));
    }
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] &= (u8) ~((u8)(src >> (8 - shift)));
      }
    }
  }
}

// Light framebuffer pixels in a 4-neighbor ring around each lit sprite
// pixel — the "white outline" pass of the inverted-sprite scheme.
// Companion to clear_sprite_progmem: call this first, then clear, and the
// sprite reads as a dark body surrounded by a bright halo. Works on any
// background because the halo is guaranteed-lit where the body is dark
// and vice-versa.
//
// Implementation: compute `outline` per column as (src | (src<<1) |
// (src>>1) | prev_col | next_col) AND-NOT src. This is the set of pixels
// adjacent to a lit source pixel but not themselves lit. OR into fb.
// dup-ok: same column-iteration framing as draw_sprite_progmem; the
// per-pixel computation differs meaningfully (4-neighbor outline math).
void draw_sprite_outline_progmem(i16 x, i16 y, u8 width, const u8* flash_src) {
  const i16 y_top = y;
  const u8 shift  = (u8)(y_top & 7);
  const i8 page   = (i8)(y_top >> 3);

  for (u8 col = 0; col < width; ++col) {
    const i16 px = x + (i16)col;
    if (px < 0 || px >= WIDTH) continue;

    const u8 src       = pgm_read_byte(&flash_src[col]);
    const u8 src_left  = (col > 0) ? pgm_read_byte(&flash_src[col - 1]) : 0;
    const u8 src_right = (col + 1 < width) ? pgm_read_byte(&flash_src[col + 1]) : 0;

    // Neighbor mask: every bit within 1 step of a lit source bit.
    const u8 neighbor = (u8)(src | (u8)(src << 1) | (u8)(src >> 1) | src_left | src_right);
    // Outline = neighbor minus body (AND-NOT src).
    const u8 outline = (u8)(neighbor & ~src);

    if (page >= 0 && page < (i8)(HEIGHT / 8)) {
      buffer[(u16)px + (u16)page * WIDTH] |= (u8)(outline << shift);
    }
    if (shift != 0) {
      const i8 page2 = page + 1;
      if (page2 >= 0 && page2 < (i8)(HEIGHT / 8)) {
        buffer[(u16)px + (u16)page2 * WIDTH] |= (u8)(outline >> (8 - shift));
      }
    }
  }
}

}  // namespace fb
