// Frame pacing. Implementation is per-platform.
//
// The engine exposes one call: `wait_for_next_frame()`. It blocks until the
// next 60Hz tick has elapsed since the previous call. The platform layer
// is responsible for setting up whatever timer/interrupt drives this.

#pragma once

#include "types.h"

namespace clock {

constexpr u8 FRAMES_PER_SEC = 60;

// Initialize the timer hardware. Call once at boot.
void init();

// Block until the next 60Hz frame boundary. Returns the number of frames
// missed (0 = on time; >0 = the frame took longer than 1/60s and we slipped).
u8 wait_for_next_frame();

// Monotonically-increasing frame counter. Useful for animation timing
// without needing per-entity timers.
extern volatile u32 frame_count;

// Microsecond clock for fine-grained timing within a frame.
//
// Wraps every (2^16 * resolution) microseconds — at the AVR's 16us tick
// resolution that's ~1 second, which is fine because we always measure
// short intervals (sub-frame work, well under 16.67ms).
//
// Use as: `u16 t0 = clock::micros(); ...do work...; u16 elapsed = clock::micros() - t0;`
// Subtraction handles wrap correctly thanks to unsigned overflow semantics.
u16 micros();

}  // namespace clock
