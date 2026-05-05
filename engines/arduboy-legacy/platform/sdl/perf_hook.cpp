// SDL: forward engine perf-hook calls to the per-frame sim-cycle counter.
//
// Op IDs in engine/perf_hook.h are kept in lock-step with perf::SimOp.
// Mismatches are caught at compile time via static_assert below.

#include "perf_hook.h"
#include "perf.h"

#include <type_traits>

namespace perf_hook {

// Compile-time guard: the two enum value sets must match exactly. If
// someone adds a new op to one without the other, the static_asserts
// fail and the build breaks — better than silent miscounting.
static_assert((u8)LZ77_DECODE == (u8)perf::SIM_LZ77_DECODE, "perf op id drift");
static_assert((u8)DISPLAY_FLUSH == (u8)perf::SIM_DISPLAY_FLUSH, "perf op id drift");
static_assert((u8)FB_CLEAR == (u8)perf::SIM_FB_CLEAR, "perf op id drift");
static_assert((u8)FB_INVERT_ALL == (u8)perf::SIM_FB_INVERT_ALL, "perf op id drift");
static_assert((u8)FB_DITHER == (u8)perf::SIM_FB_DITHER, "perf op id drift");
static_assert((u8)FONT_DRAW_CHAR == (u8)perf::SIM_FONT_DRAW_CHAR, "perf op id drift");
static_assert((u8)SPRITE_BLIT == (u8)perf::SIM_SPRITE_BLIT, "perf op id drift");
static_assert((u8)PGM_READ == (u8)perf::SIM_PGM_READ, "perf op id drift");

void charge(Op op, u16 units) {
  perf::sim_charge((perf::SimOp)op, (u32)units);
}

}  // namespace perf_hook
