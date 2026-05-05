// Minimal LZ77 decoder for compressed PROGMEM image data.
//
// Use case: full-screen 1-bit images with long runs (sky, ground) but
// non-trivial detail elsewhere. Greedy tile-extraction needs byte-equal
// 16x16 chunks; LZ77 finds repetitions at any byte offset and any length,
// so it catches structure tile mode misses.
//
// Format (byte-aligned, no bit packing — picked for decoder simplicity):
//   0xxxxxxx                : N literals follow (N = 1..127, 7-bit length)
//   10xxxxxx YYYYYYYY       : back-ref: copy (xxxxxx + 3) bytes from
//                             (current_out - YYYYYYYY - 1). Length 3..66,
//                             offset 1..256.
//   11000000                : END terminator
//
// Trade-offs vs. richer LZ77 variants: byte-aligned is ~10-15% larger
// than bit-packed, but the decoder fits in <100 B. We pay once per image
// type — at 200+ B savings per image, the decoder pays for itself with
// the first image and each subsequent one is pure win.
//
// Bake side: scripts/_lz77_bake.py compresses bytes from images.cpp.

#pragma once

#include "types.h"

namespace lz77 {

// Decode a compressed PROGMEM stream into `out`. The caller must ensure
// `out` is large enough — typically 1024 bytes for a full-screen image.
// Returns the number of bytes written.
//
// `src` is a PROGMEM address; reads go through pgm_read_byte. The
// destination is plain RAM (typically fb::buffer or a dedicated stage
// buffer copied to fb).
u16 decode(const u8* src, u8* out);

// Decode a compressed stream sourced from FX flash. Same output format
// and semantics as decode(), but the source bytes are pulled from FX
// SPI flash starting at `fx_offset`. Uses a small internal read-ahead
// window so the SPI overhead is amortized — total decode cost for a
// ~1 KB compressed source is roughly 1 ms (well under one frame at
// 60 Hz). Don't call from the audio ISR — the SPI read sequence
// holds the bus and would interleave with display flushes badly.
u16 decode_from_fx(u32 fx_offset, u8* out);

}  // namespace lz77
