#pragma once

#include "gameplay/Actor.h"

#include <glm/vec3.hpp>

#include <optional>
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
    std::string id;                  // unique-within-region; required
    std::string archetype;           // archetype lookup id; required
    glm::vec3 pos = glm::vec3(0.0f); // world XYZ; required
    // If true, pos.y is IGNORED and the spawn-time code resolves Y
    // via selva::world::groundHeight(pos.x, pos.z, +infinity-bias-fallback).
    // Authored as the string "auto_terrain" in JSON pos[1] (vs a
    // float literal Y). Useful for actors on terrain whose Y is
    // tedious to hand-pick (slopes, future-circle plains where the
    // author works in 2D maps); literal Y still works (16 limbo
    // shades all use literal -43.13). Wolf is the first user.
    bool pos_y_auto_terrain = false;
    float yaw = 0.0f;                // facing radians; optional, default 0
    bool permanent_on_death = false; // keepers=true (do not respawn on cycle); shades=false
    std::vector<glm::vec3>
        patrol_path; // optional roaming waypoints; ignored until AI_Roaming lands

    // Optional scripted-walk target authored at spawn time. When
    // present, the actor's scripted_target_pos is seeded from this on
    // spawn, and LeafFollowScriptedTarget (first node in every tree's
    // root selector) walks the actor toward it. On arrival, the leaf
    // clears the target -- the actor falls through to the rest of the
    // tree (combat/alerted/idle). Used by the soul-larvae system:
    // fresh larvae spawn at the top of the descent and walk
    // to the shore landing; scripted_stop_range gates "near enough."
    // Absent in JSON = no
    // scripted target on spawn (existing behavior).
    std::optional<glm::vec3> scripted_target_pos;
    float scripted_stop_range = 0.5f; // meters; "near enough" radius
    // Optional intermediate waypoints traversed BEFORE
    // scripted_target_pos. The leaf walks to scripted_path_waypoints[0],
    // then [1], ..., then scripted_target_pos. Use when the straight-
    // line path from spawn to target would cut through geometry --
    // e.g. fresh larvae must reach the corridor exit before angling
    // toward their scattered slot positions. Empty = single-target
    // (legacy) behavior.
    std::vector<glm::vec3> scripted_path_waypoints;

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

    // Per-flag spawn overrides. After resetCycleEnemies restores the
    // actor to its base pos/yaw, this list is walked in JSON order;
    // the LAST entry whose `flag` is set on the active profile wins
    // and overrides pos/yaw. Empty list = always spawn at base pos.
    //
    // Use case: NPCs that move after story beats. The Guide spawns
    // inside the chapel, then after the player commits to the Signing
    // he stands outside on the plateau forever after. JSON:
    //   "post_flag_positions": [
    //     { "flag": "signing_committed", "pos": [...], "yaw": ... }
    //   ]
    struct FlagPosition
    {
        std::string flag;
        glm::vec3 pos = glm::vec3(0.0f);
        bool pos_y_auto_terrain = false;
        float yaw = 0.0f;
    };
    std::vector<FlagPosition> post_flag_positions;
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

// Initialize this actor's HP / stamina / poise / poise-state to
// their pre-combat baselines. Runs the formula-derived pool init
// from Body + Stats, then layers the actor's archetype's
// max_hp_override and max_poise_override on top if either is set.
//
// One funnel for every callsite that needs to "restart" an actor's
// stat pools -- spawnEnemyFromDecl at first spawn, resetCycleEnemies
// at cycle boundaries, applyArchetypeSwap on conversion, and any
// future hook (save/load, transformation) that needs the same
// contract. Each callsite calling initActorPools alone misses the
// archetype overrides; each calling spawnEnemyFromDecl's override
// block by hand drifts. Centralizing here keeps the "form default
// -> formula -> archetype override" contract in one place. Per
//
// Caller is responsible for setting a.archetype + a.form + a.body
// + a.stats first -- this function reads them, doesn't write them.
void initActorPoolsForArchetype(Actor& a);

// Swap the actor's archetype to `target` in-place. Generalized form
// of the prior convertFreshLarvaToAged: works for any source->target
// archetype pair. Used by the on_scripted_arrival arrival-action
// system in selva::spawn::ArrivalActions. Effects, applied atomically:
//
//   - Sets a.archetype = &target
//   - Sets a.faction = target.faction (so swaps can flip e.g.
//     Neutral->Hostile)
//   - Calls initActorPoolsForArchetype(a) to re-derive HP / poise
//     from the new archetype's overrides on top of form/formula
//   - Clears a.action_state (cooldowns from prior archetype's
//     actions are meaningless for the new one)
//   - If target.aggro_clip is non-empty AND the actor's skeleton
//     has it loaded, fires the clip as a movement-locking one-shot
//     and sets a.aggro_already_fired so the perception transition
//     hook won't re-fire it on first sighting moments later
//
// Returns false on no-op (actor dead, target null). Idempotent for
// callers that need to swap-or-skip.
bool applyArchetypeSwap(Actor& a, const EnemyArchetype& target);

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
// so the AI faces + approaches the right direction) -- the rule is:
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
