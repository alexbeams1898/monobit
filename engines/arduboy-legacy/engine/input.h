// Engine-facing input API. Implementation is per-platform.
//
// Two states the game cares about:
//   - held():    is the button currently down?
//   - pressed(): was the button pressed *this frame* (rising edge)?
//
// `pressed` is what you want for menu navigation, dash, shoot-on-press, etc.
// `held` is what you want for movement.

#pragma once

#include "types.h"

namespace input {

enum Button : u8 {
  UP    = 0,
  DOWN  = 1,
  LEFT  = 2,
  RIGHT = 3,
  A     = 4,
  B     = 5,
  COUNT = 6,
};

// Called once per frame by the engine before update(). Reads raw hardware
// state and computes edge-detection deltas vs. last frame.
void poll();

bool held(Button b);
bool pressed(Button b);

}  // namespace input
