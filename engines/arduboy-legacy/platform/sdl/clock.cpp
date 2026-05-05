// SDL2 frame pacing.
//
// Strategy: record the wake-time of each frame, sleep/wait until the
// next 60Hz boundary. SDL_Delay sleeps in 1ms granularity; we use a
// short spin for the tail to land accurately on the boundary.
//
// `frame_count` increments once per returned frame so game code that
// derives animation phase from it (e.g. press_a_phase) stays in sync
// with the Arduboy behavior.

#include "clock.h"

#include <SDL.h>

namespace clock {

volatile u32 frame_count = 0;

namespace {
// Target period in milliseconds: 1000/60 ~= 16.667. We track error in
// fractional ms via a fixed-point accumulator so the long-run rate is
// exactly 60 Hz, not 60.something.
constexpr u32 PERIOD_NUM = 1000;  // numerator: ms * 60
constexpr u32 PERIOD_DEN = 60;    // denominator

uint64_t next_deadline_ticks = 0;  // SDL_GetTicks64() target for next wake
bool armed                   = false;
}  // namespace

void init() {
  // Nothing to configure: SDL_Init(SDL_INIT_VIDEO) from display::init()
  // also enables SDL_GetTicks. We arm the deadline on the first wait.
  armed = false;
}

u8 wait_for_next_frame() {
  const uint64_t now = SDL_GetTicks64();
  if (!armed) {
    next_deadline_ticks = now + (PERIOD_NUM / PERIOD_DEN);
    armed               = true;
    ++frame_count;
    return 0;
  }
  // How many full frames have we missed? (delta >= period) means the
  // last frame's work ran over budget.
  u8 missed = 0;
  if (now >= next_deadline_ticks) {
    const uint64_t behind = now - next_deadline_ticks;
    missed                = (u8)(behind / (PERIOD_NUM / PERIOD_DEN));
    // Advance deadline past all missed frames plus the next one.
    next_deadline_ticks += (uint64_t)(missed + 1) * (PERIOD_NUM / PERIOD_DEN);
    ++frame_count;
    return missed;
  }
  // Sleep close to the deadline (leave a tiny margin to spin for
  // sub-millisecond accuracy). SDL_Delay is coarse; spinning the last
  // ms keeps the wake tight without burning CPU.
  const uint64_t wait_ms = next_deadline_ticks - now;
  if (wait_ms > 1) SDL_Delay((Uint32)(wait_ms - 1));
  while (SDL_GetTicks64() < next_deadline_ticks) { /* spin */
  }
  next_deadline_ticks += (PERIOD_NUM / PERIOD_DEN);
  ++frame_count;
  return 0;
}

u16 micros() {
  // SDL_GetPerformanceCounter at perf-freq gives us real microseconds.
  // The engine only uses this for sub-frame diffing, so the u16 wrap is
  // fine (same semantic as the Arduboy version).
  const uint64_t c = SDL_GetPerformanceCounter();
  const uint64_t f = SDL_GetPerformanceFrequency();
  // Convert to microseconds: c * 1e6 / f. Do it in 64 bits to avoid
  // overflow, then truncate to u16. f is typically 1e7 or 1e9, so
  // c*1e6 / f for c ~ seconds fits comfortably.
  const uint64_t us = (c * 1000000ull) / f;
  return (u16)us;
}

}  // namespace clock
