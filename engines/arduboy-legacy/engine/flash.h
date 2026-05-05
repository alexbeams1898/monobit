// Engine-side abstraction for "read N bytes from non-volatile program memory."
//
// On Arduboy this maps to AVR's PROGMEM via memcpy_P. On a hypothetical
// non-AVR 1-bit platform (e.g. desktop simulator) the implementation can
// just be a regular memcpy. Engine code calls flash::read(...); only the
// platform layer touches AVR-specific intrinsics.

#pragma once

#include "types.h"

namespace flash {

// Copy `n` bytes from `src` (which lives in program memory / flash) into
// `dst` (in RAM). Equivalent to memcpy(dst, src, n) semantically.
void read(void* dst, const void* src, u16 n);

}  // namespace flash
