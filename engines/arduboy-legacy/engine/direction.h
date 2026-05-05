// 8-direction enum + canonical unit vectors.
//
// Used for: player movement, player facing, future enemy facing/movement.
// Storing direction as a discrete enum (rather than a free-form fx vector)
// keeps animation/sprite logic simple — there are only 9 cases (8 + NONE).
//
// Unit vectors are 8.8 fixed-point. Cardinal = ±1.0. Diagonal = ±sin(45°)
// ≈ ±0.7071 ≈ 181/256 — close enough for an arcade game.

#pragma once

#include "fixed.h"
#include "types.h"

namespace dir {

enum Dir : u8 { NONE = 0, N = 1, NE = 2, E = 3, SE = 4, S = 5, SW = 6, W = 7, NW = 8, COUNT };

// Pre-baked unit vectors (in 8.8 fixed-point).
//   NONE      -> (0, 0)
//   N         -> (0, -1)
//   NE        -> (+0.707, -0.707)
//   E         -> (+1, 0)
//   ...
struct Vec {
  fx dx;
  fx dy;
};

constexpr fx DIAG = 181;  // ~0.707 in 8.8 (256 * sqrt(2)/2 ≈ 181)

// Unit-vector table lives in read-only storage (PROGMEM on AVR, .rodata
// on PC). Defined in direction.cpp. Not exposed directly because on AVR
// the pointer is a flash address and dereferencing it as RAM reads garbage
// — always go through read(Dir).
//
// NOTE: progmem.h is deliberately NOT included from this header. Pulling
// in <avr/io.h> (transitively via <avr/pgmspace.h>) drags in macros like
// `SE` that would collide with our Dir::SE enumerator. The read() helper
// is defined out-of-line in direction.cpp for the same reason.
Vec read(Dir d);

// Convert raw input deltas (-1, 0, +1) to a Dir.
constexpr Dir from_input(i8 mx, i8 my) {
  if (mx == 0 && my == 0) return NONE;
  if (mx == 0 && my < 0) return N;
  if (mx > 0 && my < 0) return NE;
  if (mx > 0 && my == 0) return E;
  if (mx > 0 && my > 0) return SE;
  if (mx == 0 && my > 0) return S;
  if (mx < 0 && my > 0) return SW;
  if (mx < 0 && my == 0) return W;
  return NW;
}

}  // namespace dir
