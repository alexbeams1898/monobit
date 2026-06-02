#pragma once

#include <string>

// Boss reward dispatcher: called from the boss-death cleanup path
// when a boss actor dies. Owns the boss-specific reward logic
// (item drops, sangue grants, Grimoire unlocks, world-state flips).
//
// v1: code-dispatch by spawn_decl_id (one case per shipped boss).
// Future: when enough bosses ship, becomes data-driven via fields
// on EnemyArchetype (e.g. `drops`, `grimoire_unlock`, etc.). Coding
// it generically now is premature -- the "how rewards work
// mechanically" question hasn't been answered yet, so keep it
// imperative for now.
//
// Per docs/design/ideas/boss_backend.md section 12 + impl plan
// step 11.

namespace selva::gameplay
{

struct Actor;

// Called once per confirmed boss death. boss_id is the boss's
// spawn-decl id (e.g. "lupa"); killer is the actor whose hit
// landed the killing blow (typically the player, but defensive
// to support faction-conflict in the future). No-op for unknown
// boss_ids (they just don't get a reward).
void onBossFelled(const std::string& boss_id, Actor& killer);

} // namespace selva::gameplay
