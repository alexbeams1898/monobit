// Engine-side 1-bit framebuffer.
//
// Layout matches the SSD1306 GDDRAM in horizontal addressing mode exactly,
// so the platform layer can flush it to the display with a straight memcpy
// over SPI — no transposition cost.
//
//   - 1024 bytes total: WIDTH * HEIGHT / 8
//   - one byte = a vertical column of 8 pixels
//   - bit 0 of byte = top pixel, bit 7 = bottom
//   - byte index = x + (y / 8) * WIDTH
//
// All drawing happens to this in-memory buffer; the display is updated only
// once per frame via the platform's flush().

#pragma once

#include "types.h"

namespace fb {

constexpr u8 WIDTH  = 128;
constexpr u8 HEIGHT = 64;
constexpr u16 SIZE  = (u16)WIDTH * HEIGHT / 8;  // 1024

extern u8 buffer[SIZE];

// Set every pixel to off. ~1024 stores; cheap.
void clear();

// Replace the entire framebuffer with a 1024-byte PROGMEM blob in the same
// page-major layout. Used for full-screen background images on title /
// game-over / pause / cutscene screens. Caller passes a PROGMEM pointer
// (e.g. `&IMAGE_data[0]`); the function reads from flash directly.
void draw_full_image(const u8* progmem_data);

// Single pixel. No-op if (x, y) is off-screen.
void set_pixel(i16 x, i16 y);

// Force a single pixel to OFF (the default draw ops only OR bits in;
// these are needed for true overlays and inverse text).
void clear_pixel(i16 x, i16 y);

// Filled rectangle of arbitrary size, clipped to screen bounds.
// Dumb but small — used for procedural sprites and HP bars.
void fill_rect(i16 x, i16 y, u8 w, u8 h);

// Set every pixel inside the rect to OFF (for clearing space behind an overlay).
void clear_rect(i16 x, i16 y, u8 w, u8 h);

// XOR every byte with 0xFF — black becomes white, white becomes black.
// Use at the END of a frame's draw to flip the whole screen into "light
// mode" for pre-Hell screens (title, name entry, main menu, dark-wood
// dialog). Cheap: ~1024 cycles on AVR.
void invert_all();

// In-place Bayer-mask invert. `level` is 0..16:
//   level == 0   -> invert every pixel (equivalent to invert_all)
//   level == 16  -> invert nothing (no-op)
//   in between   -> invert only pixels whose 4x4 Bayer threshold is >= level,
//                   producing a dither pattern that thins as level rises.
//
// Use to dissolve from a dark/inverse view of the framebuffer back to its
// natural form (e.g. dark-mode -> light-mode wood-return transition):
// render the destination normally, then dither_invert with level rising
// from 0 -> 16 over the transition.
void dither_invert(u8 level);

// 4x4 Bayer threshold at (x, y). Returns 0..15. Use with a level (0..16)
// to drive your own dither: a pixel is "revealed" iff threshold < level.
// Shared so screen-specific fades (gate ritual, etc.) don't keep their
// own copies of the Bayer table.
u8 bayer_threshold(u8 x, u8 y);

// 1-pixel-thick outlined rectangle, clipped.
void stroke_rect(i16 x, i16 y, u8 w, u8 h);

// Blit an 8-tall sprite of the given width into the framebuffer, ORing
// pixels in (so transparent = "off" in the source).
//
// `sprite` is laid out the same way the framebuffer is: one byte per
// 8-pixel-tall column, left-to-right. So an 8x8 sprite is 8 bytes; a
// 16x8 sprite is 16 bytes.
//
// y can be any value; we shift bytes vertically as needed. (x, y) is the
// top-left corner. Off-screen portions are clipped.
void draw_sprite(i16 x, i16 y, u8 width, const u8* sprite);

// Same as draw_sprite, but reads source bytes from read-only storage
// (PROGMEM on AVR, plain pointer on PC) via the progmem shim. Used by
// the game's sprite-table renderer to avoid a big RAM scratch buffer.
void draw_sprite_progmem(i16 x, i16 y, u8 width, const u8* flash_src);

// Mirror-horizontal variant of draw_sprite_progmem: column 0 of the
// source draws to the rightmost target column, column W-1 to the
// leftmost. Per-byte data isn't bit-flipped (vertical bits stay top-up);
// only column order is reversed. Used for left-facing animation frames
// when the spritesheet only contains right-facing art — saves half the
// flash budget for animations.
void draw_sprite_progmem_mirrored(i16 x, i16 y, u8 width, const u8* flash_src);

// "Black body" variant: wherever the sprite is lit, AND-NOT the
// corresponding framebuffer pixel off. Used with a mostly-lit background
// so the sprite appears as a cut-out silhouette. Pair with
// draw_sprite_outline_progmem for guaranteed legibility over any field.
void clear_sprite_progmem(i16 x, i16 y, u8 width, const u8* flash_src);

// RAM-source counterpart to clear_sprite_progmem. Used to punch out
// silhouettes of LZ77-decoded sprites (e.g. boss portraits) from a
// RAM-resident cache.
void clear_sprite(i16 x, i16 y, u8 width, const u8* sprite);

// "White outline" variant: sets every framebuffer pixel adjacent (4-way)
// to a lit sprite pixel that isn't itself part of the sprite body. Call
// FIRST (before clear_sprite_progmem) so the body's cleared pixels don't
// get re-lit by the outline pass.
void draw_sprite_outline_progmem(i16 x, i16 y, u8 width, const u8* flash_src);

}  // namespace fb
