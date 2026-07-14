#pragma once

#include "PlayerConfig.h"

#include <entt/entt.hpp>

class EntityManager;

namespace world_init
{
// Spawns the player at the region center: Transform + Velocity + foot-anchored
// Collider + animated Sprite + active follow Camera. Sprite/animation layout
// comes from the PlayerConfig (config over constants). Returns the player entity.
// Requires the tile map to be loaded first (reads its dimensions for the center).
entt::entity spawnPlayer(EntityManager& em, const PlayerConfig& cfg);
} // namespace world_init
