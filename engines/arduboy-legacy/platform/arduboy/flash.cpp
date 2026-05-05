// Arduboy implementation of the engine's flash::read interface.
//
// On AVR, "flash" means program memory (PROGMEM). Reads use the special
// LPM instruction and avr-libc's memcpy_P wrapper, NOT a plain memcpy
// (which would read from RAM and get garbage).

#include "flash.h"

#include <avr/pgmspace.h>
#include <stddef.h>

namespace flash {

void read(void* dst, const void* src, u16 n) {
  memcpy_P(dst, src, (size_t)n);
}

}  // namespace flash
