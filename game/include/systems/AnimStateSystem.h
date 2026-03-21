#pragma once

#include "ecs/EntityManager.h"

// AnimStateSystem -- resolves animation state from game component state.
// Reads Dead, DamageFeedback, AttackLocked, Velocity and writes anim.state.
// Runs each game tick after combat systems so anim.state is current for the render pass.
// AnimationSystem (engine) reads anim.state and advances frames; it does not set state.
class AnimStateSystem
{
  public:
    static void update(EntityManager& em);
};
