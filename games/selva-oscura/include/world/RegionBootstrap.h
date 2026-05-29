#pragma once

#include "world/Region.h"

#include <string>

namespace selva::world
{

// Boot the scene system: read assets/regions/regions.json, parse each
// listed scene's region.json, register a JsonRegion per entry with the
// engine RegionManager. Returns the RegionId of the default-spawn
// scene (or kInvalidRegion on failure).
//
// Must run AFTER engine::physics::initPhysics and AFTER the rest of
// game-side asset modules (Tunables, audio.json, etc.) so scenes can
// reference assets that exist.
engine::world::RegionId loadAllRegions();

// The default-spawn scene id, set by loadAllRegions. Used by new-
// character spawn to know where to put the player.
engine::world::RegionId defaultSpawnRegion();

} // namespace selva::world
