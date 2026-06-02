#pragma once

#include "gameplay/Actor.h"

#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace selva::gameplay
{

// Enemy is just an Actor. The historic separate struct was merged
// into the unified actor pool — every enemy is now an Actor with
// controller != Controller::Input, stored alongside the player in
// the global pool. This alias keeps existing call sites compiling
// during the migration; new code should refer to Actor directly.
using Enemy = Actor;

// Declarative spawn record parsed from a region.json's enemy_spawns
// array. Authored content; JsonRegion produces these and hands them
// to spawnRegionEnemies on region activation. Schema lives in
// assets/regions/SCHEMA.md.
struct EnemySpawnDecl
{
    std::string id;                       // unique-within-region; required
    std::string archetype;                // archetype lookup id; required
    glm::vec3 pos = glm::vec3(0.0f);      // world XYZ; required
    // If true, pos.y is IGNORED and the spawn-time code resolves Y
    // via selva::world::groundHeight(pos.x, pos.z, +infinity-bias-fallback).
    // Authored as the string "auto_terrain" in JSON pos[1] (vs a
    // float literal Y). Useful for actors on terrain whose Y is
    // tedious to hand-pick (slopes, future-circle plains where the
    // author works in 2D maps); literal Y still works (16 limbo
    // shades all use literal -43.13). Wolf is the first user.
    bool pos_y_auto_terrain = false;
    float yaw = 0.0f;                     // facing radians; optional, default 0
    bool permanent_on_death = false;      // keepers=true (do not respawn on cycle); shades=false
    std::vector<glm::vec3> patrol_path;   // optional roaming waypoints; ignored until AI_Roaming lands

    // ----------------------------------------------------------------
    // Boss fields (per boss_backend.md). Only consulted when the
    // archetype's is_boss=true. All default to empty / zero so
    // non-boss spawns carry no boss semantics.
    // ----------------------------------------------------------------

    // Pattern A (generic trigger-spawned boss): trigger id whose
    // firing causes this enemy to spawn (instead of boot-spawning).
    // Empty = boot-spawn (existing behavior; shades, ambient
    // enemies, AND Pattern B bosses use boot-spawn).
    std::string spawn_trigger_id;

    // Pattern B (already-there boss like Lupa): trigger id whose
    // firing transitions this (boot-spawned) boss from
    // archetype.initial_state to combat-ready, playing
    // archetype.engage_clip once. Empty = boss has no engage
    // trigger (either it's combat-ready from spawn, or Pattern A).
    std::string engage_trigger_id;

    // Arena lockout AABB (world-space). While this boss is alive
    // and engaged, the player is contained inside this AABB
    // (soft push-back at boundary, velocity preserved). Zero
    // half_extents = no lockout (free-roaming fight). Per-boss,
    // not per-region (different bosses in same region can have
    // different arenas).
    glm::vec3 arena_center = glm::vec3(0.0f);
    glm::vec3 arena_half_extents = glm::vec3(0.0f);
};

// Spawn every enemy declared in the given region's enemy_spawns into
// the shared actor pool. Idempotent for the same region: re-call
// after a clear/reset replaces the same set. The actor's spawn_id
// gets prefixed with "region_id:" so two regions can share local ids
// without collision.
void spawnRegionEnemies(const std::string& region_id, const std::vector<EnemySpawnDecl>& decls);

// Spawn a single enemy from a decl. Exposed for trigger-spawned
// bosses (per docs/design/ideas/boss_backend.md Pattern A): the
// boss skips at boot, then the BossDispatcher calls this directly
// when the spawn trigger fires.
void spawnEnemyFromDecl(const std::string& region_id, const EnemySpawnDecl& decl);

void shutdownHubEnemies();

// Cycle-respawn: reset every non-permanent AI actor in the pool back
// to its spawn baseline (position/yaw, full pools, perception cleared,
// death/knockdown flags cleared, sampler one-shot released, intent
// zeroed). Permanent-on-death actors that are dead stay dead;
// permanent-on-death actors that are alive also reset.
//
// Hooked into:
//   - Player second-death respawn (new cycle begins; per setting.md
//     cycle structure, Hell re-streams shades into their punishment
//     positions every cycle).
//   - New Game / Load Game transitions (clean world per save load).
//
// Replaces the per-enemy timer-respawn that was wired before. The
// cycle-flow model is the canonical respawn semantics per docs/
// design/setting.md "Per-circle reactivity" + "Cycle structure".
void resetCycleEnemies();

// Per-frame tick: advance each enemy's animation. dt is real-time
// seconds. Player animation tick lives in the player's per-frame
// logic; this handles the AI-controlled actors only.
void tickEnemies(float dt);

// Transition a scripted-death actor from Engaged to Dying NOW: plays
// the archetype's pain clip, freezes locomotion, opens an
// input-locking Scene if none is active. No-op if the actor is not
// Engaged or has no scripted-death configuration.
void beginScriptedDying(Actor& actor);

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

// Reverse of enemyIndex: returns the Actor pointer for the given
// index in the filtered enemies() view, or nullptr if the index is
// out of range. Used by combat-side code (HitVolumes) that has an
// OwnerRef and needs to query the owning actor's state.
const Actor* enemyAt(int index);

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
