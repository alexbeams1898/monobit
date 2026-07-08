#pragma once

class EntityManager;

// Populates the world for a region. Scaffold stage: builds a small code-generated
// placeholder region (walkable interior, solid border) directly into the
// EntityManager's TileMap + TileConfig, so the render path can be proven before
// the LDtk importer exists. The importer replaces this data source in a later
// slice step -- the render path downstream does not change.
namespace world_init
{
void buildPlaceholderRegion(EntityManager& em);
}
