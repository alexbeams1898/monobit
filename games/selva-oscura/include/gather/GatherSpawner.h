#pragma once

// Wood gather-node spawner. Maintains a unified population of
// procedurally-placed pickup nodes across the Wood region. ONE flow
// per zone (today: `config/gather_nodes/wood_forage.json`); each new
// spawn rolls the flow's WeightedPool drop_pool to pick which material
// instantiates. Adding a new Wood material is one entry in drop_pool;
// no new flow needed.
//
// Architecture: cap + interval loop, mirroring FlowSpawner. The flow
// declares an active_cap; when the live count drops below it AND the
// interval has elapsed since the last spawn, the spawner places one
// new node via rejection sampling and rolls its material from the
// pool. Material + quality + yaw are all rolled at spawn time and
// persisted in PlayerProfile.active_gather_nodes per the anti-cheese
// doctrine -- quit-reload returns to committed state.
//
// Diverges from FlowSpawner in TWO important ways:
//   1. Persistence: state lives in PlayerProfile, not in module-local
//      memory. Reload restores the world.
//   2. Spawns INTERACTABLES (pickup-style), not ACTORS. No archetypes,
//      no BTs, no on-arrival actions.
//
// See [[project_healing_system_locked_2026_06_14]] +
// [[project_anti_cheese_rolls_locked_2026_06_14]].

namespace selva::gather
{

// Load every config/gather_nodes/*.json into the in-memory NodeConfig
// registry. Called once at boot AFTER itemRegistry is populated (so
// material refs validate). Idempotent.
void initGatherSpawner();

// Per-frame tick. Ticks globally regardless of active region: the
// spawner's timer reads wallClock() and Wood gather respawn happens
// while the player is in Hell. Reads/writes the active PlayerProfile's
// gather state.
//
// Per tick, for each loaded flow:
//   1. Count live nodes in active_gather_nodes.
//   2. If live < active_cap AND (initial fill not yet done OR
//      wallClock() - flow.last_spawn >= interval), attempt one spawn:
//      roll material from drop_pool, sample placement, roll quality,
//      commit NodeState. Success: flow.last_spawn = wallClock().
//   3. Register Interactables for any active_gather_nodes not yet
//      wired (handles save/load and region transitions).
void tickGatherSpawner(float dt);

// Tear down + reset on hard reset (character switch, second-death
// cycle). Clears active_gather_nodes + gather_flows in the active
// profile and unregisters all Interactables. The next tick re-runs
// initial fill from the loaded NodeConfigs.
void resetGatherSpawner();

} // namespace selva::gather
