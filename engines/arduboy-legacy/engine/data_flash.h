// Engine-side abstraction for "read N bytes from external data flash."
//
// On Arduboy FX this maps to the W25Q128 SPI flash chip on the cart;
// on stock Arduboy (no FX cart) reads return 0xFF. On SDL it maps to
// a file `data.bin` next to the executable; missing-file or past-EOF
// reads also return 0xFF.
//
// Empty-byte convention: 0xFF, matching the natural erased state of
// NOR flash. Callers that need a sentinel for "asset not present"
// should use 0xFF (never 0x00 — that's a valid asset byte).
//
// Use case: storage for content too big to live in internal program
// flash — songs (4-voice patterns), full bestiary sprites, dialog
// trees, level layouts. Reads cost ~10x more than PROGMEM (an SPI
// transaction per byte vs a single LPM cycle), so this is for
// once-per-frame-or-less data, NOT the audio ISR hot path. Anything
// the audio mixer reads at 16 kHz must stay in PROGMEM.
//
// Address space is a flat 0..16M byte offset. The build pipeline
// generates an offset header (data_offsets.h) by concatenating
// FX-bound assets in a known order — game code references symbols
// from that header, never raw addresses.

#pragma once

#include "types.h"

namespace data_flash {

// Copy `n` bytes from `offset` (a 0-based byte offset into the FX
// data image) into `dst` (in RAM). On platforms without FX storage,
// fills `dst` with 0xFF and returns silently.
//
// Blocking. Cost on Arduboy FX with SPI at fosc/2 (8 MHz): about
// 1 µs per byte plus ~6 µs setup. A 256 B read is ~262 µs ≈ 4 frame
// cycles at 60 fps. Don't call this in the audio ISR; do call it
// once per frame or less from update() / draw().
void read(u32 offset, void* dst, u16 n);

// Convenience: read a single byte. Same cost characteristics as
// read(); use sparingly, batch reads via read() when possible.
u8 read_byte(u32 offset);

// ---- Save region ----------------------------------------------------
//
// 8 KB region split into two 4 KB ping-pong sectors for player saves
// (vestigia + autosave ledger). See games/rpg/vestigia.cpp.
constexpr u16 SECTOR_SIZE = 4096;
constexpr u8 SECTOR_COUNT = 2;
constexpr u16 SAVE_SIZE   = SECTOR_SIZE * SECTOR_COUNT;  // 8192

void save_read(u16 save_offset, void* dst, u16 n);
bool save_write_page(u16 save_offset, const void* src, u16 n);
bool save_erase_sector(u8 sector);

}  // namespace data_flash
