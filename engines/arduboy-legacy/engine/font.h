// Tiny 3-wide x 5-tall bitmap font. Columns are stored as bytes with bits
// 0..4 used (bits 5..7 zero), so the regular sprite blitter can draw them.
//
// Stored as 4 bytes per glyph: 3 columns of pixel data + 1 column of
// inter-character spacing. Total: 4 bytes/char.
//
// Glyph set (intentionally minimal — just what the HUD/menus need):
//   '0'..'9'  digits
//   'A'..'Z'  uppercase letters
//   ' '       space
//   ':'       colon (for "WAVE: 5")
//   '>'       menu selector
//   '~'       middle dot (beat separator: `HEED ~ STRIKE ~ CHOOSE`)
//   '!'       exclamation (for the death-beat closer on THE DESCENT)
//   ','       comma (no descender — bottom-row tick at the baseline)
//
// Periods (`.`) are intentionally absent. Hell's text has no period —
// sentences end at line breaks. `*` renders as the Latin interpunct
// (middle-dot, ·) — used both for the HUD's circle/round separator
// AND as the ornamental separator in the SELVA OSCURA frieze
// alternating with the dagger †.

#pragma once

#include "types.h"

namespace font {

constexpr u8 GLYPH_W = 3;  // visible columns per glyph
constexpr u8 GLYPH_H = 5;
constexpr u8 STRIDE  = 4;  // bytes per glyph in the table (3 cols + 1 space)

// Look up a glyph; returns nullptr for unsupported chars.
const u8* glyph(char c);

// Draw one character at (x, y). Returns the x advance (always STRIDE for now).
u8 draw_char(i16 x, i16 y, char c);

// Draw a null-terminated string. Stops at '\0'. No wrapping.
//
// `s` MUST be a RAM pointer — bare literals (`"TEXT"`) land in .data on
// AVR (RAM-resident with a flash-init copy), wasting RAM. For literals,
// use draw_text_pgm with a `PROGMEM`-declared symbol.
void draw_text(i16 x, i16 y, const char* s);

// PROGMEM-aware variant. `pgm` points at flash; each char is read via
// pgm_read_byte. Preferred form for any string declared as
// `const char NAME[] PROGMEM = "..."` — keeps the bytes out of RAM.
void draw_text_pgm(i16 x, i16 y, const char* pgm);

// Draw an unsigned integer (right-justified at x_right). Useful for HUD numbers.
void draw_uint(i16 x_right, i16 y, u16 value);

// Inverse variants: clear (zero) the glyph pixels instead of setting them.
// Use when drawing text on a filled-white background (overlays, menus).
u8 draw_char_inverse(i16 x, i16 y, char c);
void draw_text_inverse(i16 x, i16 y, const char* s);
void draw_text_inverse_pgm(i16 x, i16 y, const char* pgm);

// Overlined variants: draw the normal glyph and paint a 3-pixel horizontal
// stroke two rows above the glyph top. This is the Roman vinculum — an
// overline on a letter multiplies its value by 1,000 (M̄ = 1,000,000,
// X̄ = 10,000). Period-correct medieval accountancy notation. The caller
// must reserve the two pixel rows above the text (y >= 2).
u8 draw_char_overlined(i16 x, i16 y, char c);

}  // namespace font
