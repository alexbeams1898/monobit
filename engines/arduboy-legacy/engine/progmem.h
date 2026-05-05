// Portable shim for "this constant lives in read-only storage."
//
// On AVR the table must go in flash (PROGMEM) because RAM is 2.5 KB and
// we can't spend it on fixed lookup data. On any other platform the
// distinction is meaningless — plain globals already live in .rodata.
//
// Game/engine code uses the familiar AVR names (PROGMEM attribute on the
// declaration, pgm_read_* to read). This header maps them to the real AVR
// intrinsics when __AVR__ is defined, and to no-ops / plain dereferences
// otherwise. The net effect: one header swap, zero call-site changes.
//
// Only platform/arduboy/ is allowed to include <avr/pgmspace.h> directly.

#pragma once

#include "types.h"

#ifdef __AVR__

#include <avr/pgmspace.h>

// AVR pointers are 16-bit; pgm_read_word yields the stored pointer value
// correctly. pgm_read_ptr is provided by avr-libc on newer versions; if
// the user has an older toolchain it maps cleanly to pgm_read_word.
#ifndef pgm_read_ptr
#define pgm_read_ptr(p) ((void*)pgm_read_word(p))
#endif

#else

// Non-AVR: PROGMEM is just an attribute-less marker, the reads are plain
// pointer dereferences.
#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(p) (*(const u8*)(p))
#endif

#ifndef pgm_read_word
#define pgm_read_word(p) (*(const u16*)(p))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(p) (*(const u32*)(p))
#endif

// Pointer-sized read. On AVR this is 2 bytes; on a 64-bit PC it's 8.
// Use this — NOT pgm_read_word — whenever a PROGMEM table stores C
// pointers (e.g. `const char* const TABLE[] PROGMEM`). Old AVR code
// that assumed pgm_read_word would return a pointer is a bug on PC.
#ifndef pgm_read_ptr
#define pgm_read_ptr(p) (*(void* const*)(p))
#endif

#ifndef memcpy_P
#include <string.h>
#define memcpy_P(dst, src, n) memcpy((dst), (src), (n))
#endif

#endif  // __AVR__
