// 8.8 fixed-point arithmetic.
//
// Why fixed-point: AVR has no FPU, so floats are software-emulated and slow.
// At 60Hz with 30+ entities each running velocity math, that adds up. Fixed
// is one or two integer ops per coordinate — fast and deterministic.
//
// Format: int16_t where the high 8 bits = whole part, low 8 bits = fraction.
//   1.0  = 0x0100 = 256
//   0.5  = 0x0080 = 128
//   2.5  = 0x0280 = 640
//  -1.0  = 0xFF00 (two's complement)
//
// Range: -128.0 .. +127.99... pixels, in 1/256 px steps. Enough for our
// 128x64 screen and any reasonable bullet speed.

#pragma once

#include "types.h"

using fx = i16;

constexpr fx FX_ONE  = 256;
constexpr fx FX_HALF = 128;

// Construct from a whole pixel.
constexpr fx fx_px(i16 px) {
  return (fx)(px << 8);
}
// Construct from numerator/denominator at compile time, e.g. fx_div(3, 2) for 1.5.
constexpr fx fx_div(i16 n, i16 d) {
  return (fx)(((i32)n << 8) / d);
}
// Floor to integer pixel. Used when handing a position back to the renderer.
constexpr i16 fx_to_px(fx v) {
  return (i16)(v >> 8);
}
