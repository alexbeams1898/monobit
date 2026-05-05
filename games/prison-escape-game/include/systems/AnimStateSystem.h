#pragma once

#include "ecs/EntityManager.h"

// AnimStateSystem -- resolves animation state from game components and writes
// the resulting row/frames/duration into the engine Animation component.
class AnimStateSystem
{
  public:
    static void update(EntityManager& em);
};
