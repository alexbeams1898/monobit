#include "font.h"

#include "framebuffer.h"
#include "progmem.h"

namespace font {

namespace {

// 3-col x 5-row glyphs, packed into bytes (bit 0 = top row, bit 4 = bottom).
// Each glyph is 3 used columns + 1 spacing column (0).
const u8 glyphs[] PROGMEM = {
    // ' ' (space)        '!'   '"'   '#'   - we only fill what we need.
    0b00000, 0b00000, 0b00000, 0,  // ' '
    // '0'-'9'
    0b01110, 0b10001, 0b01110, 0,  // 0
    0b00010, 0b11111, 0b00000, 0,  // 1
    0b11001, 0b10101, 0b10010, 0,  // 2
    0b10001, 0b10101, 0b01010, 0,  // 3
    0b00111, 0b00100, 0b11111, 0,  // 4
    0b10111, 0b10101, 0b01001, 0,  // 5
    0b01110, 0b10101, 0b01001, 0,  // 6
    0b00001, 0b11101, 0b00011, 0,  // 7
    0b01010, 0b10101, 0b01010, 0,  // 8
    0b10010, 0b10101, 0b01110, 0,  // 9
    // ':'
    0b00000, 0b01010, 0b00000, 0,  // :
    // '>'  (5 rows: top to bottom of the 5-px glyph; bit 0 = top)
    0b10001, 0b01010, 0b00100, 0,  // >
    // Sangue drop. Source char is '~' (ASCII, unused elsewhere). Renders
    // as a chunky 3x4 teardrop with apex up and fat belly — "one drop of
    // Phlegethon," the unit-glyph after every sangue count. Also reused
    // as a beat separator in CONTROLS body text (`HEED ~ STRIKE ~ CHOOSE`)
    // where it now reads as a small drop instead of a centered dot.
    0b01100, 0b11111, 0b01100, 0,  // ~ -> sangue drop
    // '!' — tall stroke in the middle column, gap, dot at the bottom.
    // Rows 0..2 filled, row 3 blank, row 4 filled (bit 4). Drawn only in
    // the center column so adjacent chars get proper kerning.
    0b00000, 0b10111, 0b00000, 0,  // !
    // ',' — bottom-row pixel in the center column with a tail-dot below
    // (row 4 lit). The 3x5 height has no descender, so this is the closest
    // a comma can get. Reads as "tick at the baseline" beside its neighbor.
    0b00000, 0b11000, 0b00000, 0,  // ,
    // '+' -> dagger (†). Used as an ornamental flourish around the SELVA
    // OSCURA hub header — period-correct for a Commedia register, and the
    // wood is where the suicides will bleed in canto XIII (the imagery
    // rhymes). Vertical staff with a crossbar at row 2.
    0b00100, 0b11111, 0b00100, 0,  // + -> dagger
    // '*' -> middle-dot (·). Latin interpunct — single pixel at row 2,
    // center column. Used both as the SELVA OSCURA hub frieze separator
    // (alternating with the dagger †) AND as the HUD's circle/round
    // separator. One ornament for both contexts; the dagger carries the
    // visual weight in the frieze. Period-correct: the interpunct is
    // the standard Latin word/numeral separator from inscriptions and
    // ledgers across the Dantean era.
    0b00000, 0b00100, 0b00000, 0,  // * -> middle-dot

    // 'A'-'Z'
    0b11110, 0b00101, 0b11110, 0,  // A
    0b11111, 0b10101, 0b01010, 0,  // B
    0b01110, 0b10001, 0b10001, 0,  // C
    0b11111, 0b10001, 0b01110, 0,  // D
    0b11111, 0b10101, 0b10001, 0,  // E
    0b11111, 0b00101, 0b00001, 0,  // F
    0b01110, 0b10001, 0b11101, 0,  // G
    0b11111, 0b00100, 0b11111, 0,  // H
    0b10001, 0b11111, 0b10001, 0,  // I
    0b01000, 0b10000, 0b01111, 0,  // J
    0b11111, 0b00100, 0b11011, 0,  // K
    0b11111, 0b10000, 0b10000, 0,  // L
    0b11111, 0b00010, 0b11111, 0,  // M  (compact)
    0b11111, 0b00010, 0b11110, 0,  // N
    0b01110, 0b10001, 0b01110, 0,  // O
    0b11111, 0b00101, 0b00010, 0,  // P
    0b01110, 0b11001, 0b11110, 0,  // Q
    0b11111, 0b00101, 0b11010, 0,  // R
    0b10010, 0b10101, 0b01001, 0,  // S
    0b00001, 0b11111, 0b00001, 0,  // T
    0b01111, 0b10000, 0b01111, 0,  // U
    0b00111, 0b11000, 0b00111, 0,  // V
    0b11111, 0b01000, 0b11111, 0,  // W  (compact)
    0b11011, 0b00100, 0b11011, 0,  // X
    0b00011, 0b11100, 0b00011, 0,  // Y
    0b11001, 0b10101, 0b10011, 0,  // Z
};

// Map a char to its glyph index. Returns 0xFF for unsupported.
u8 index_of(char c) {
  if (c == ' ') return 0;
  if (c >= '0' && c <= '9') return 1 + (u8)(c - '0');  // 1..10
  if (c == ':') return 11;
  if (c == '>') return 12;
  if (c == '~') return 13;
  if (c == '!') return 14;
  if (c == ',') return 15;
  if (c == '+') return 16;
  if (c == '*') return 17;
  if (c >= 'A' && c <= 'Z') return 18 + (u8)(c - 'A');  // 18..43
  return 0xFF;
}

}  // namespace

const u8* glyph(char c) {
  u8 i = index_of(c);
  if (i == 0xFF) return nullptr;
  return &glyphs[(u16)i * STRIDE];
}

u8 draw_char(i16 x, i16 y, char c) {
  const u8* g = glyph(c);
  if (g) {
    u8 ram[STRIDE];
    for (u8 i = 0; i < STRIDE; ++i)
      ram[i] = pgm_read_byte(&g[i]);
    fb::draw_sprite(x, y, GLYPH_W, ram);
  }
  return STRIDE;
}

void draw_text(i16 x, i16 y, const char* s) {
  while (*s) {
    x += draw_char(x, y, *s);
    ++s;
  }
}

// PROGMEM-flavored variant. Pass a `const char NAME[] PROGMEM = "..."`
// pointer; the loop reads each char via pgm_read_byte. This is the
// preferred form for bare UI literals — `font::draw_text(x, y, "TEXT")`
// places the literal in .data (RAM-resident, with a flash-init copy)
// and burns 1 byte of RAM per char + NUL. The PROGMEM form burns flash
// only.
void draw_text_pgm(i16 x, i16 y, const char* pgm) {
  for (;;) {
    char c = (char)pgm_read_byte(pgm);
    if (c == 0) break;
    x += draw_char(x, y, c);
    ++pgm;
  }
}

void draw_text_inverse_pgm(i16 x, i16 y, const char* pgm) {
  for (;;) {
    char c = (char)pgm_read_byte(pgm);
    if (c == 0) break;
    x += draw_char_inverse(x, y, c);
    ++pgm;
  }
}

u8 draw_char_inverse(i16 x, i16 y, char c) {
  const u8* g = glyph(c);
  if (g) {
    // Walk the glyph bit-by-bit and clear (zero) each set source pixel.
    for (u8 col = 0; col < GLYPH_W; ++col) {
      u8 byte = pgm_read_byte(&g[col]);
      for (u8 row = 0; row < GLYPH_H; ++row) {
        if (byte & (1 << row)) {
          fb::clear_pixel(x + (i16)col, y + (i16)row);
        }
      }
    }
  }
  return STRIDE;
}

void draw_text_inverse(i16 x, i16 y, const char* s) {
  while (*s) {
    x += draw_char_inverse(x, y, *s);
    ++s;
  }
}

u8 draw_char_overlined(i16 x, i16 y, char c) {
  draw_char(x, y, c);
  for (u8 col = 0; col < GLYPH_W; ++col) {
    fb::set_pixel(x + (i16)col, y - 2);
  }
  return STRIDE;
}

void draw_uint(i16 x_right, i16 y, u16 value) {
  // Build digits right-to-left.
  char buf[6];  // up to 5 digits + null
  u8 n = 0;
  if (value == 0) {
    buf[n++] = '0';
  } else {
    while (value > 0 && n < 5) {
      buf[n++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }
  // Draw, advancing leftward from x_right.
  i16 x = x_right - STRIDE;
  for (u8 i = 0; i < n; ++i) {
    draw_char(x, y, buf[i]);
    x -= STRIDE;
  }
}

}  // namespace font
