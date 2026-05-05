// Engine-side perf instrumentation hook.
//
// Hot-path engine functions (LZ77 decode, framebuffer ops, font draw,
// sprite blit) call `perf_hook::charge(OP, units)` to record AVR-cycle
// estimates. The Arduboy platform stubs this out to nothing — production
// builds carry zero perf overhead. The SDL platform forwards to
// `perf::sim_charge()` so PC test runs produce simulated AVR-cycle
// budgets per frame, and `make sdl-perf-check` can fail the build if
// any frame goes over the 16 MHz / 60 Hz budget (266,667 cycles).
//
// Why this header exists: engine/ may not include platform/. Without
// this thin shim, every engine TU would have to #ifdef SDL/Arduboy.
// Now hot paths just call charge() and the linker resolves it to the
// active platform's implementation. Same pattern as engine/flash.h.
//
// Op IDs match perf::SimOp in platform/sdl/perf.h. Adding a new op:
//   1. Add the enum value here.
//   2. Add cost in platform/sdl/perf.cpp's COST_PER_UNIT.
//   3. Add cost entry in tools/perf_logger/avr_costs.json.
//   4. Mirror the enum in platform/sdl/perf.h's SimOp.

#pragma once

#include "types.h"

namespace perf_hook {

enum Op : u8 {
  LZ77_DECODE    = 0,  // charge per output byte
  DISPLAY_FLUSH  = 1,  // charge per flush (constant)
  FB_CLEAR       = 2,  // charge per clear (constant 1024 bytes)
  FB_INVERT_ALL  = 3,  // charge per invert (constant 1024 bytes)
  FB_DITHER      = 4,  // charge per dither (constant 1024 bytes + table)
  FONT_DRAW_CHAR = 5,  // charge per char drawn
  SPRITE_BLIT    = 6,  // charge per sprite byte
  PGM_READ       = 7,  // charge per PROGMEM byte read
};

// Charge `units` units of work for `op`. Cost-per-unit is in the
// platform's cost table. On Arduboy this is a no-op and gets inlined
// to nothing.
void charge(Op op, u16 units);

}  // namespace perf_hook
