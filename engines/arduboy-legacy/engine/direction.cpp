#include "direction.h"

#include "progmem.h"

// Single definition of the unit-vector table. Lives in PROGMEM on AVR
// (.progmem.data section -> flash-resident, never copied to RAM), in
// .rodata on any other target. Read exclusively through dir::read() —
// indexing this array directly is undefined behavior on AVR.
//
// NOTE: progmem.h is included here (not in direction.h) because it pulls
// in <avr/io.h> transitively, which #defines register-bit macros like
// `SE` that would collide with the Dir::SE enumerator.

namespace dir {

namespace {
const Vec UNIT[COUNT] PROGMEM = {
    {0, 0},          // NONE
    {0, -FX_ONE},    // N
    {DIAG, -DIAG},   // NE
    {FX_ONE, 0},     // E
    {DIAG, DIAG},    // SE
    {0, FX_ONE},     // S
    {-DIAG, DIAG},   // SW
    {-FX_ONE, 0},    // W
    {-DIAG, -DIAG},  // NW
};
}  // namespace

Vec read(Dir d) {
  Vec v;
  v.dx = (fx)pgm_read_word(&UNIT[d].dx);
  v.dy = (fx)pgm_read_word(&UNIT[d].dy);
  return v;
}

}  // namespace dir