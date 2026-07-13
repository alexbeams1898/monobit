#pragma once

#include "PlayerConfig.h"

#include <entt/entt.hpp>

class EntityManager;
namespace observations
{
struct State;
}

// Populates the world for a region. Scaffold stage: builds a small code-generated
// placeholder region (walkable interior, solid border) directly into the
// EntityManager's TileMap + TileConfig, so the render path can be proven before
// the LDtk importer exists. The importer replaces this data source in a later
// slice step -- the render path downstream does not change.
namespace world_init
{
void buildPlaceholderRegion(EntityManager& em);

// Stamp a tile per authored observable at its config coords, chosen by the
// observable's `kind`. Call after observations::load so config is the single
// source of truth for where the observable tiles are (no hand-synced entries).
void placeObservableTiles(EntityManager& em, const observations::State& obs);

// Spawns the player at the region center: Transform + Velocity + foot-anchored
// Collider + animated Sprite + active follow Camera. Sprite/animation layout
// comes from the PlayerConfig (config over constants). Returns the player entity.
entt::entity spawnPlayer(EntityManager& em, const PlayerConfig& cfg);
} // namespace world_init
