#pragma once

#include "world/Region.h"

#include <string>

namespace selva::world
{

// Boot the scene system, PHASE 1: read assets/regions/regions.json,
// parse each listed scene's region.json, register a JsonRegion per
// entry with the engine RegionManager + register each region's
// authored terrain modifiers into the global modifier registry.
// Returns the RegionId of the default-spawn scene (or kInvalidRegion
// on failure).
//
// MUST run BEFORE initTerrain() so terrain modifiers are present when
// the mesh builder samples per-vertex Y. The 2-phase split exists
// because phase 2 (loadAllRegionsPreload) needs the terrain mesh to
// exist (it builds Jolt shapes for the mesh), while phase 1 needs to
// register modifiers that influence the mesh build itself.
//
// Must run AFTER engine::physics::initPhysics and AFTER the rest of
// game-side asset modules (Tunables, audio.json, etc.) so scenes can
// reference assets that exist.
engine::world::RegionId loadAllRegionsRegister();

// Boot the scene system, PHASE 2: iterate the regions registered in
// phase 1 + call preloadAssets() on each (.glb loading + GPU upload
// + per-region terrain Jolt shape construction). MUST run AFTER
// initTerrain() because the terrain CPU vertex arrays must exist for
// shape construction.
void loadAllRegionsPreload();

// The default-spawn scene id, set by loadAllRegionsRegister. Used by new-
// character spawn to know where to put the player.
engine::world::RegionId defaultSpawnRegion();

// Pull every resident region's enemy_spawn_decls and hand them to
// selva::gameplay::spawnRegionEnemies. Call ONCE at boot after the
// archetype registry + behavior trees are loaded (their absence at
// activateRegionImmediate time is why region commitPrepared CAN'T
// spawn enemies itself).
//
// Multi-resident architecture: every region's bodies + meshes are
// in physics/render at all times, so every region's enemies must
// be too. Per-region cycle-respawn handles state reset across the
// whole pool when the Vagrant dies / starts a new game.
void spawnAllRegionEnemies();

// Walk every JsonRegion's prop decls and realize them via the prop
// spawn funnel. Sibling of spawnAllRegionEnemies; same multi-resident
// rationale. Call ONCE at boot after propArchetypes().loadDirectory.
// v1 only appends cylinders for category=Tree props onto the global
// CollisionRegion; lights deferred until the Limbo-lights step.
void spawnAllRegionProps();

} // namespace selva::world
