#pragma once

#include "ecs/GameComponents.h"

#include <vector>

#include <entt/entt.hpp>

class EntityManager;

namespace hit_area
{

// Move travelling areas, apply damage to whatever is newly inside them, and retire the expired.
void update(EntityManager& em, float dt);

} // namespace hit_area
