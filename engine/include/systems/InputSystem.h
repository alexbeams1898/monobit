#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// InputSystem — translates raw SDL keyboard state into Input component values.
//
// Call once per frame from Engine::processEvents(), AFTER SDL_PollEvent has
// drained the event queue. SDL_GetKeyboardState reflects the latest state only
// after PollEvent runs, so order matters.
//
// Writes moveX/moveY to every entity that has an Input component. Today that
// is just the player, but the system doesn't need to know that.
//
// Does NOT write to Velocity directly — that is MovementSystem's job. Keeping
// the two decoupled means we can later add gamepad support, replays, or AI
// override without touching movement logic.
// ---------------------------------------------------------------------------

class InputSystem
{
  public:
    static void update(EntityManager& em);
};
