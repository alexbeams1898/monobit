#pragma once

#include "ecs/GameComponents.h"

#include <vector>

#include <entt/entt.hpp>

class EntityManager;

namespace hit_area
{

// Move travelling areas, apply damage to whatever is newly inside them, and retire the expired.
void update(EntityManager& em, float dt);

// Nothing is mid-flash any more. A flash is a fraction of a second of a thing being struck, and
// a world that stops ticking holds it exactly as it was rather than letting it finish.
void forget(EntityManager& em);

} // namespace hit_area
