// 60Hz frame pacing for the Arduboy.
//
// Strategy: Timer1 in CTC mode fires every 1/60s and increments a `tick`
// counter. The main loop calls wait_for_next_frame(), which sleeps the CPU
// until tick != last_seen_tick. This keeps timing rock-solid even if a
// single frame's update+draw runs short — we always wake on the timer edge.
//
// At F_CPU = 16 MHz with prescaler 256, one timer step = 16us.
// 1/60s ≈ 16667us, so OCR1A = 16667 / 16 - 1 ≈ 1041.

#include "clock.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

namespace clock {

volatile u32 frame_count = 0;

namespace {
volatile u8 tick = 0;  // wraps; we only ever compare against `last_seen`
u8 last_seen     = 0;
}  // namespace

void init() {
  // CTC mode (WGM12=1), prescaler /256 (CS12=1).
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS12);
  OCR1A  = 1041;           // ~60.02 Hz; close enough
  TIMSK1 = (1 << OCIE1A);  // enable compare-match interrupt

  set_sleep_mode(SLEEP_MODE_IDLE);  // wake on any interrupt; keeps timers running
  sei();                            // global interrupt enable
}

u8 wait_for_next_frame() {
  // Sleep until the timer ISR bumps `tick`. CPU draws ~zero current here —
  // not that it matters in the emulator, but it does on real hardware.
  //
  // Race-safe pattern: check tick under cli(), enter sleep with sei()
  // immediately before sleep_cpu(). AVR guarantees an interrupt pending
  // at sei cannot fire until after the next instruction executes, so the
  // sei/sleep pair is atomic — the ISR can't slip between "I decided to
  // sleep" and "I'm asleep" and lose the wakeup. Without this, a frame
  // whose work finishes just as the timer ISR fires can end up sleeping
  // an entire extra period, manifesting in the emulator as the CPU%
  // meter pinning to 100 (the sleep never registers).
  while (true) {
    cli();
    if (tick != last_seen) break;
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
  }
  u8 delta  = (u8)(tick - last_seen);
  last_seen = tick;
  frame_count += delta;
  sei();
  return (u8)(delta - 1);
}

// Microseconds since boot (mod 65536). Combines the per-frame tick counter
// (incremented every ~16.67ms by ISR) with TCNT1 (the running counter
// inside the current period at 16us resolution).
//
//   total_us = tick * 16667 + TCNT1 * 16
//
// We disable interrupts briefly during the read so a tick++ can't land
// between reading `tick` and TCNT1.
u16 micros() {
  u8 t;
  u16 c;
  u8 sreg = SREG;
  cli();
  t    = tick;
  c    = TCNT1;
  SREG = sreg;
  // We only return a u16, so the multiplication truncates naturally —
  // adequate for sub-frame timing (one frame ≈ 16667 us, well under u16 max).
  return (u16)((u16)t * (u16)16667) + (u16)((u16)c * (u16)16);
}

}  // namespace clock

ISR(TIMER1_COMPA_vect) {
  ++clock::tick;
}
