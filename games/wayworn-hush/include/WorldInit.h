#pragma once

#include "PlayerConfig.h"

#include <entt/entt.hpp>

class EntityManager;

// Populates the world for a region. Scaffold stage: builds a small code-generated
// placeholder region (walkable interior, solid border) directly into the
// EntityManager's TileMap + TileConfig, so the render path can be proven before
// the LDtk importer exists. The importer replaces this data source in a later
// slice step -- the render path downstream does not change.
namespace world_init
{
void buildPlaceholderRegion(EntityManager& em);

// Spawns the player at the region center: Transform + Velocity + foot-anchored
// Collider + animated Sprite + active follow Camera. Sprite/animation layout
// comes from the PlayerConfig (config over constants). Returns the player entity.
entt::entity spawnPlayer(EntityManager& em, const PlayerConfig& cfg);
} // namespace world_init
