// Platform-neutral UART byte emitter for perf-logging.
//
// Engine code calls uart_log::write_byte(b) to push raw bytes out the
// platform's debug-serial channel. The Arduboy backend uses USART1. SDL
// backends can dump to stdout or a file. Release builds don't link this
// TU at all (the game-side perf code is gated behind DEBUG + PERF_UART_LOG).

#pragma once

#include "types.h"

namespace uart_log {

// Initialize the platform's serial channel. Safe to call repeatedly; the
// platform implementation must be idempotent. Called lazily on the first
// write_byte if not called explicitly.
void init();

// Write one byte. Platform decides the cadence/semantics (polling vs.
// buffered). Expected to be fast enough to call 3x per 60Hz frame with
// room to spare — at 500,000 baud on USART1 that's 180 bytes/sec, well
// under the 62,500 bytes/sec the hardware can carry.
void write_byte(u8 b);

}  // namespace uart_log
