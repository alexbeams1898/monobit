// Arduboy entry point.
//
// Tiny by design: bring the platform up, hand control to the game's
// update/draw, flush the framebuffer, sleep until the next frame.
//
// No perf instrumentation. All profiling / timing / overlay work lives
// in the SDL (PC) build under tools/perf_logger — the Arduboy ships
// exactly the game and nothing else, keeping the 28KB flash ceiling
// free for gameplay. Verify perf on PC via `make sdl-build-log`, sanity-check
// on Ardens before release, flash a real Arduboy occasionally.

#include "audio.h"
#include "clock.h"
#include "dbg.h"
#include "display.h"
#include "framebuffer.h"
#include "game.h"
#include "input.h"
#include "stack_probe.h"

// (Old multi-second LED bisect beacons retired — they bloated the
// PLAY bank past the rcall horizon when DBG_TRACE was enabled, and
// the ring-buffer trace API serves the same purpose at far smaller
// flash cost. See engine/dbg.h for the current trace mechanism.)

// Boot-trace tag map (DBG_TRACE=1). Ardens serial console / USART
// window shows these as plain ASCII lines. The LAST line before the
// crash tells us how far boot got.
//   T00  main entered (very first instruction)
//   T01  display::init() returned
//   T02  clock::init() returned
//   T03  audio::init() returned
//   T04  game::init() returned
//   T05  loop entered (first iteration only)
// Boot tracepoints (DBG_TRACE=1). Each fires a fast UART line +
// brief LED pulse. The .init3 / .init8 markers above (multi-second
// LED holds) confirmed boot reaches main. Inside main we use the
// fast trace API — the multi-second holds were fine for boot
// bisection but starve the audio ISR (which fires at 16 kHz; 30+
// seconds without audio::tick yields a corrupted-state crash that
// masked the real bug).
//
//   T01  display::init() returned
//   T02  clock::init() returned
//   T03  audio::init() returned
//   T04  game::init() returned
//   T05  loop entered (first iteration only)
int main() {
  // Paint the unused stack region with a sentinel so stack_probe::peak_bytes()
  // can read empirical high-water at any point. Zero cost when STACK_PROBE
  // is undefined. Must be the very first thing in main: anything pushed
  // before paint becomes invisible to the probe.
  stack_probe::paint();

  display::init();
  clock::init();
  audio::init();
  game::init();
  // Boot-path stack-peak snapshot. See stack_probe.h.
  stack_probe::log_peak_to_eeprom();

  for (;;) {
    input::poll();
    audio::tick();
    game::update();
    bool needs_flush = game::draw();
    if (needs_flush) display::flush();
    clock::wait_for_next_frame();
  }
}
