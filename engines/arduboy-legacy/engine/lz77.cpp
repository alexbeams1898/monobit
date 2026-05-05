// LZ77 decoder. See lz77.h for the format.

#include "lz77.h"

#include "data_flash.h"
#include "perf_hook.h"
#include "progmem.h"

namespace lz77 {

u16 decode(const u8* src, u8* out) {
  u16 si = 0;  // source index
  u16 di = 0;  // destination index
  for (;;) {
    const u8 token = pgm_read_byte(&src[si++]);
    if (token == 0xC0) break;  // END terminator
    if ((token & 0x80) == 0) {
      // Literal run: lower 7 bits = N, next N bytes are literals.
      const u8 n = token & 0x7F;
      for (u8 i = 0; i < n; ++i) {
        out[di++] = pgm_read_byte(&src[si++]);
      }
    } else {
      // Back-reference: length = (lower 6 bits) + 3, offset = (next byte) + 1.
      // Offset MUST be u16: encoded byte 0xFF means offset 256, and a
      // u8 + 1 = u8 wraps that back to 0. That bug shipped once — every
      // FOREST_LZ77 frame had ~28 corrupted back-refs flashing through.
      const u8 len     = (u8)((token & 0x3F) + 3);
      const u16 offset = (u16)(pgm_read_byte(&src[si++])) + 1;
      // Copy from earlier in the OUTPUT (RAM), not the source. Source-
      // overlap copies are intentional (length can exceed offset for
      // run-length-style expansion).
      for (u8 i = 0; i < len; ++i) {
        out[di] = out[di - offset];
        ++di;
      }
    }
  }
  perf_hook::charge(perf_hook::LZ77_DECODE, di);
  return di;
}

namespace {

// Streaming byte source for decode_from_fx. Pulls FX_WINDOW bytes at a
// time into a stack window so each individual byte read isn't its own
// SPI transaction (those carry ~6 µs setup overhead each — 929 of them
// would dominate the decode). Window size is a tradeoff: larger = less
// SPI overhead, more stack. 32 B keeps stack pressure low while
// amortizing setup over ~30× the bytes.
constexpr u8 FX_WINDOW = 32;

struct FxStream {
  u32 base;           // FX flash base offset of the compressed stream
  u32 cursor;         // next absolute FX offset to fetch
  u8 win[FX_WINDOW];  // current window contents
  u8 win_pos;         // next index within win[] to consume
  u8 win_len;         // valid bytes in win[] (= FX_WINDOW after a refill)

  void refill() {
    data_flash::read(cursor, win, FX_WINDOW);
    cursor += FX_WINDOW;
    win_pos = 0;
    win_len = FX_WINDOW;
  }

  u8 next() {
    if (win_pos >= win_len) refill();
    return win[win_pos++];
  }
};

}  // namespace

// dup-ok: the inner decode loop mirrors decode() but pulls bytes from
// FxStream::next() instead of pgm_read_byte. Factoring would require a
// virtual byte-source or a templated byte-source, neither of which is
// worth the call overhead / code-size cost on AVR for one extra caller.
u16 decode_from_fx(u32 fx_offset, u8* out) {
  FxStream src;
  src.base    = fx_offset;
  src.cursor  = fx_offset;
  src.win_pos = FX_WINDOW;  // forces refill on first next()
  src.win_len = 0;

  u16 di = 0;
  for (;;) {
    const u8 token = src.next();
    if (token == 0xC0) break;
    if ((token & 0x80) == 0) {
      const u8 n = token & 0x7F;
      for (u8 i = 0; i < n; ++i) {
        out[di++] = src.next();
      }
    } else {
      const u8 len     = (u8)((token & 0x3F) + 3);
      const u16 offset = (u16)src.next() + 1;
      // dup-ok: mirrors the back-ref loop in decode() above. Differs only
      // in token-source plumbing (FxStream vs PROGMEM); fusing would
      // require a templated/virtual byte source whose call overhead is
      // worse than the duplication on AVR.
      for (u8 i = 0; i < len; ++i) {
        out[di] = out[di - offset];
        ++di;
      }
    }
  }
  perf_hook::charge(perf_hook::LZ77_DECODE, di);
  return di;
}

}  // namespace lz77
