#pragma once

#include "ecs/EntityManager.h"

class TintSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
