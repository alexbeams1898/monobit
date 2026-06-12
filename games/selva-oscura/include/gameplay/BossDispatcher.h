#pragma once

#include <string>

// Boss-trigger dispatcher: routes Custom triggers (per
// engines/engine/include/world/Region.h TriggerAction::Custom) to
// game-side boss-lifecycle actions. The engine emits the trigger;
// this module decides what it means.
//
// Action vocabulary (convention `<verb>:<arg>`):
//   - "spawn:<spawn_decl_id>" — Pattern A trigger-spawned boss.
//     Finds the EnemySpawnDecl in the current region's enemy_spawns
//     by id and spawns it now. The decl's spawn_trigger_id should
//     match the trigger's id (defensive; not enforced).
//   - "engage:<spawn_decl_id>" — Pattern B already-there boss.
//     Finds the already-spawned actor by spawn_decl_id, clears its
//     current_boss_state, queues the archetype's engage_clip as a
//     one-shot, sets GameState.active_boss_idx + active_boss_id.
//
// Both verbs no-op gracefully if the target boss is already dead /
// missing / already engaged. The dispatcher is the single source of
// truth for the boss-side semantics; the engine remains generic.
//
// Per games/selva-oscura/docs/design/ideas/boss_backend.md section 5
// (Step 5 of impl plan).

namespace engine::world
{
struct RegionTrigger;
}

namespace selva::gameplay
{

// Called once per fresh-edge custom trigger. Inspects the trigger's
// action_payload, splits on the first ':', dispatches by verb.
// Unknown verbs are logged + ignored.
void dispatchCustomTrigger(const engine::world::RegionTrigger& trigger);

// Verb handlers. Exposed for direct call from tests + future code
// paths that bypass the trigger system. Both are idempotent against
// the actor pool: re-firing the same verb for an existing (alive or
// dead) boss is a no-op.
//   - spawn: Pattern A. Spawns the named decl now; no-op if a boss
//     with this id is already in the pool.
//   - engage: Pattern B. Wakes an already-spawned Dormant boss; no-op
//     if the boss is in any other state (Engaged, Dying, Felled) or
//     missing entirely.
void handleSpawnVerb(const std::string& spawn_id);
void handleEngageVerb(const std::string& spawn_id);

} // namespace selva::gameplay
