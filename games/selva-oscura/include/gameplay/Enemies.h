#pragma once

#include "gameplay/Actor.h"

#include <glm/vec3.hpp>

namespace selva::gameplay
{

// Enemy is just an Actor. The historic separate struct was merged
// into the unified actor pool — every enemy is now an Actor with
// controller != Controller::Input, stored alongside the player in
// the global pool. This alias keeps existing call sites compiling
// during the migration; new code should refer to Actor directly.
using Enemy = Actor;

// Initialize the hub's enemies: one stationary humanoid in the
// clearing, idling. Call once at startup after initSkeletalAssets()
// and initHubScene(). Idempotent; clears + repopulates.
//
// Appends enemy actors to the shared pool (after the player at
// index 0). The pool must already contain the player.
void initHubEnemies();
void shutdownHubEnemies();

// Per-frame tick: advance each enemy's animation. dt is real-time
// seconds. Player animation tick lives in the player's per-frame
// logic; this handles the AI-controlled actors only.
void tickEnemies(float dt);

// Read-only enemy view — the actors in the shared pool whose
// controller is NOT Input. Returned as a std::vector by value so
// existing call sites (range-for over a span) keep working. The
// view is a snapshot; mutations to enemies should go through the
// shared pool via actors() in Actor.h.
std::vector<Actor*> enemies();

// Index of `actor` in the filtered enemies() view, or -1 if not
// present. Mirrors how hurtbox spawn keys OwnerRef{Enemy, i}; the
// AI action-fire path needs the same id to spawn its hitbox.
int enemyIndex(const Actor& actor);

// Fire a hit-react on the given enemy (indexed into the filtered
// enemy view, see enemies()). Applies poise damage first; if poise
// breaks (current <= 0) fires the knockdown chain (knockdown clip
// followed by getting_up). Otherwise picks an HP-damage-tiered
// reaction (flinch / hit_react_medium / hit_react_heavy / death).
// `world_normal` is the attacker -> target xz direction at hit time.
// `attacker_pos` is the attacker's world position; used to instantly
// aggro the enemy to Combat awareness (sets last_known_player_pos
// so the AI faces + approaches the right direction) — Souls rule:
// getting hit always engages, even if you were sneaking up from
// behind and out of the vision cone.
// No-op on out-of-range or unloaded clips, or while the actor is
// already knocked down / dead (hit-immunity).
void playEnemyHitReact(int index, int damage, int poise_damage, const glm::vec3& world_normal,
                       const glm::vec3& attacker_pos);

// Fire the death one-shot on `e` and mark it dead. Reads
// `e.death_clip_name` (enemies: "death"; PC: "second_death") so each
// actor controls its own death visual. Caller already confirmed
// hp <= 0. `index` is the actor's slot in actors() — used for the
// [death] log line; pass 0 for the player.
void fireEnemyDeath(Actor& e, int index);

} // namespace selva::gameplay
