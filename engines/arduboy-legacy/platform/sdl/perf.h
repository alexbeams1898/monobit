// SDL-only performance instrumentation + AVR-cycle simulator.
//
// The Arduboy build has no perf code whatsoever — all profiling lives
// here. On PC the frame cost is irrelevant (a handful of milliseconds
// on modern hardware), but we accumulate a *simulated AVR cycle count*
// per frame so a PC test run produces "this would have been X% of the
// Arduboy's 16 MHz / 60 Hz budget" alongside real wall-clock numbers.
//
// Hot-path engine functions (LZ77 decode, display flush, dither, font
// render) have cost estimates in tools/perf_logger/avr_costs.json.
// The SDL wrappers call `sim::charge(OP_ID, n_bytes_or_calls)` each
// time the work happens — the analyzer converts that to cycles using
// the calibration table.
//
// Binary output format (3 bytes per frame) over stdout when
// PERF_UART_LOG is defined:
//   byte 0: state_id  (u8)  — game's State enum value
//   byte 1: us_lo     (u8)
//   byte 2: us_hi     (u8)  — wall-clock us for this frame (u16 LE)
// Followed by per-frame simulated-cycle payload:
//   byte 3: cycles_b0 (u8)
//   byte 4: cycles_b1 (u8)
//   byte 5: cycles_b2 (u8)
//   byte 6: cycles_b3 (u8)  — simulated AVR cycles (u32 LE)
// Total: 7 bytes/frame = 420 B/s at 60Hz. tools/perf_logger parses it.

#pragma once

#include "types.h"

namespace perf {

// Frame-level timing (wall clock).
void frame_begin();
void frame_end();

// Record the game's current State enum value (or any u8 tag). Emitted at
// frame_end in the binary log stream. Game code calls this whenever the
// state changes — the logger caches the last value and stamps each
// frame with it. Safe to call every frame; it's a u8 store.
void set_log_state(u8 state);

// Simulated AVR cycle counter for this frame. Engine-function wrappers
// in the SDL platform call sim_charge() with the operation id and the
// work unit (e.g. bytes decoded, pixels drawn). Zeroed at each
// frame_begin.
//
// Operation ids are stable across builds and match the keys in
// tools/perf_logger/avr_costs.json. Adding a new hot-path wrapper: pick
// the next id, add its entry to the JSON, rebuild sim_costs.h from it.
enum SimOp : u8 {
  SIM_LZ77_DECODE    = 0,  // charge per output byte
  SIM_DISPLAY_FLUSH  = 1,  // charge per flush (constant ~2ms on AVR)
  SIM_FB_CLEAR       = 2,  // charge per clear (constant 1024 bytes)
  SIM_FB_INVERT_ALL  = 3,  // charge per invert (constant 1024 bytes)
  SIM_FB_DITHER      = 4,  // charge per dither (constant 1024 bytes + table)
  SIM_FONT_DRAW_CHAR = 5,  // charge per char drawn
  SIM_SPRITE_BLIT    = 6,  // charge per sprite byte
  SIM_PGM_READ       = 7,  // charge per PROGMEM byte read
  SIM_OP_COUNT       = 8,
};

void sim_charge(SimOp op, u32 units);
u32 sim_frame_cycles();  // current frame's accumulated simulated cycles

}  // namespace perf
