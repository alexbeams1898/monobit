#pragma once

#include <glm/vec2.hpp>

namespace engine::world
{

// A structure footprint declares an XZ rect where terrain must be
// removed — both VISUALLY (terrain fragment shader discards pixels)
// and PHYSICALLY (terrain trimesh has those quads dropped).
//
// One registration call covers both sides so they can't drift apart.
// This is the canonical primitive for "carve terrain to make room
// for a building's interior + any tunnel/shaft extensions."
//
// See games/selva-oscura/docs/design/DEV_PILLARS.md#8.
struct StructureFootprint
{
    glm::vec2   center_xz{0.0f, 0.0f};
    glm::vec2   half_extents_xz{0.0f, 0.0f};
    const char* debug_name = nullptr;
};

// Register a footprint. Internally:
//   1. Adds a Hole-mode TerrainModifier at this rect (physics drop)
//   2. Pushes to the footprint list that terrain rendering queries
//      to set up shader-discard rects.
//
// MUST be called BEFORE initTerrain() so the physics trimesh respects
// the hole at mesh-build time.
void registerStructureFootprint(const StructureFootprint& f);

// Clear all registered footprints (used by scene transitions).
void clearStructureFootprints();

// Count + accessor for render code that wants to read the list.
int  structureFootprintCount();
const StructureFootprint& structureFootprintAt(int idx);

} // namespace engine::world
