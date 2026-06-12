#pragma once

// FlowSpawner: a general spawn-and-flow system. Loads
// config/spawn_flows/*.json at boot; each flow describes a
// periodic-spawn loop for one population of actors (active cap,
// spawn interval, source archetype, spawn anchor, optional scripted
// target + arrival action). Per-frame tick walks all flows and runs
// their spawn/arrival logic.
//
// Doctrine: this is the general foundation for "enemies that flow
// into the world over time, subject to a population cap." Soul
// larvae are the first user (per project_soul_larvae_cosmology),
// but any future enemy with population dynamics declares a flow
// JSON and gets the same machinery. One system, many flows.

namespace selva::spawn
{

// Load every *.json in config/spawn_flows/ as a Flow. Idempotent;
// called once at boot AFTER archetypes() is populated (each flow
// validates its archetype reference at load time).
void initFlowSpawner();

// Per-frame tick. Iterates loaded flows; for each:
//   - Detects new arrivals at scripted target (stamps arrival_wallclock)
//   - Fires delayed on_arrival_action for arrived actors past delay
//   - Increments per-flow spawn timer; if interval elapsed and
//     active count < cap, spawns one new actor via spawnEnemyFromDecl
void tickFlowSpawner(float dt);

// Reset per-flow timers + counters. Called from
// hardResetWorldForCharacter so the spawn cadence starts fresh
// across save loads / character switches.
void resetFlowSpawner();

} // namespace selva::spawn
