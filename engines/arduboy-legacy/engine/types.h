// Engine-wide tiny types.
//
// On AVR, `int` is 16 bits and unqualified arithmetic likes to surprise you,
// so we standardize on fixed-width types from <stdint.h> everywhere. The
// short aliases (u8/i16/...) keep call sites readable.

#pragma once

#include <stdint.h>

using u8  = uint8_t;
using i8  = int8_t;
using u16 = uint16_t;
using i16 = int16_t;
using u32 = uint32_t;
using i32 = int32_t;

// Screen-space position. i16 (not u8) so we can safely represent off-screen
// values during partial-blit clipping without underflow.
struct Vec2 {
  i16 x;
  i16 y;
};

// Mark a function as a scene-root draw/update entry point. Forces the linker
// to emit it as a real symbol instead of LTO-inlining it into game::draw() /
// game::update() / main(). Without this, scene-paging analysis (and any
// future scene-paging architecture) is impossible — the per-scene bytes
// disappear into a 10 KB main and can't be measured.
//
// Apply to: every function called from a `case STATE_X:` in game::draw()
// and game::update(). The efficiency_lint rule `core-pattern-without-noinline`
// enforces this.
#define SCENE_ROOT __attribute__((noinline))
