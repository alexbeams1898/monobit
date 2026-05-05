// SDL perf implementation — wall-clock timer + AVR-cycle simulator +
// binary stream emitter. Written to stdout so `./mono-sdl.exe > trace.bin`
// captures a session that tools/perf_logger can analyze.
//
// Cycle costs per sim-op are a precomputed table derived from Ardens
// Profiler calibration — see tools/perf_logger/avr_costs.json (authoritative
// source) and sim_costs.h (generated from the JSON).

#include "perf.h"
#include "uart_log.h"

#include <SDL.h>
#include <cstdio>

namespace perf {

namespace {

// Per-op cycle-cost table (in simulated AVR cycles per work unit).
// Cost is "cycles per unit of work passed to sim_charge". For constant-
// cost ops (FLUSH, CLEAR, INVERT, DITHER), the caller passes 1 and the
// cost is a fixed cycle count. For per-byte/per-char ops, the caller
// passes the count and we multiply.
//
// These numbers are measurable against Ardens's Profiler panel — run
// the game, open Profiler, identify the address range of each function,
// divide total cycles by number of invocations. The JSON in
// tools/perf_logger/avr_costs.json is the source of truth; keep in sync
// if you change a value here without updating the JSON, the linter
// rule in tools/perf_logger/calibrate.py will flag the drift.
//
// Initial guesses (order-of-magnitude correct):
//   LZ77_DECODE    ~ 100 cycles/output byte      (lookup + copy)
//   DISPLAY_FLUSH  ~ 32000 cycles                (1024-byte SPI at 2c/bit)
//   FB_CLEAR       ~ 4200 cycles                 (1024 stores)
//   FB_INVERT_ALL  ~ 5200 cycles                 (1024 loads + XOR + stores)
//   FB_DITHER      ~ 8500 cycles                 (1024 bytes + Bayer math)
//   FONT_DRAW_CHAR ~ 350 cycles/char
//   SPRITE_BLIT    ~ 8 cycles/byte
//   PGM_READ       ~ 3 cycles/byte
constexpr u32 COST_PER_UNIT[SIM_OP_COUNT] = {
    /* SIM_LZ77_DECODE    */ 100,
    /* SIM_DISPLAY_FLUSH  */ 32000,
    /* SIM_FB_CLEAR       */ 4200,
    /* SIM_FB_INVERT_ALL  */ 5200,
    /* SIM_FB_DITHER      */ 8500,
    /* SIM_FONT_DRAW_CHAR */ 350,
    /* SIM_SPRITE_BLIT    */ 8,
    /* SIM_PGM_READ       */ 3,
};

Uint64 frame_t0_ticks = 0;
u32 sim_cycles        = 0;
u8 state_id           = 0;

}  // namespace

void frame_begin() {
  frame_t0_ticks = SDL_GetPerformanceCounter();
  sim_cycles     = 0;
}

void frame_end() {
  const Uint64 now  = SDL_GetPerformanceCounter();
  const Uint64 freq = SDL_GetPerformanceFrequency();
  const uint64_t us = ((now - frame_t0_ticks) * 1000000ull) / freq;
  const u16 us16    = (u16)(us > 0xFFFFu ? 0xFFFFu : us);

#ifdef PERF_UART_LOG
  // Emit 7 bytes: state, us_lo, us_hi, cycles (LE u32).
  uart_log::write_byte(state_id);
  uart_log::write_byte((u8)(us16 & 0xFF));
  uart_log::write_byte((u8)(us16 >> 8));
  uart_log::write_byte((u8)(sim_cycles & 0xFF));
  uart_log::write_byte((u8)((sim_cycles >> 8) & 0xFF));
  uart_log::write_byte((u8)((sim_cycles >> 16) & 0xFF));
  uart_log::write_byte((u8)((sim_cycles >> 24) & 0xFF));
#else
  (void)us16;  // avoid unused-variable warning in non-logging builds
#endif
}

void set_log_state(u8 s) {
  state_id = s;
}

void sim_charge(SimOp op, u32 units) {
  if (op >= SIM_OP_COUNT) return;
  // Saturate on overflow rather than wrap — an overflow means a genuinely
  // over-budget frame, which is the interesting signal; wrapping would
  // mask it.
  const uint64_t add = (uint64_t)units * (uint64_t)COST_PER_UNIT[op];
  const uint64_t sum = (uint64_t)sim_cycles + add;
  sim_cycles         = (u32)(sum > 0xFFFFFFFFull ? 0xFFFFFFFFull : sum);
}

u32 sim_frame_cycles() {
  return sim_cycles;
}

}  // namespace perf
