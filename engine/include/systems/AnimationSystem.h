#pragma once

#include "ecs/EntityManager.h"

class AnimationSystem
{
  public:
    static void update(EntityManager& em, float dt);
};
