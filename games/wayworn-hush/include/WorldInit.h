#pragma once

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

// Spawns the player at the region center: Transform + Velocity + a foot-anchored
// Collider + a placeholder colored-box Sprite + an active follow Camera.
// Returns the player entity. The real 32x64 sprite replaces the box later.
entt::entity spawnPlayer(EntityManager& em);
} // namespace world_init
