// On PC there's no flash/RAM split — "PROGMEM" is just .rodata. The
// engine's flash::read(dst, src, n) is equivalent to memcpy.

#include "flash.h"

#include <cstring>

namespace flash {

void read(void* dst, const void* src, u16 n) {
  std::memcpy(dst, src, n);
}

}  // namespace flash
