#pragma once

#include <string>

// Boss reward dispatcher: called from the boss-death cleanup path
// when a boss actor dies. Owns the boss-specific reward logic
// (item drops, sangue grants, Grimoire unlocks, world-state flips).
//
// v1: code-dispatch by spawn_decl_id (one case per shipped boss).
// When enough bosses ship to define the reward shape, this becomes
// data-driven via fields on EnemyArchetype (e.g. `drops`,
// `grimoire_unlock`). Today the "how rewards work mechanically"
// question is open, so the implementation is imperative -- one
// function per boss reward, called from the felled handler.
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
