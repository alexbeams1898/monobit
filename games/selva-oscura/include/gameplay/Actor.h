#pragma once

#include "anim/PoseSampler.h"
#include "gameplay/Perception.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

// Unified actor model — the PC/NPC symmetry rule, made concrete in
// code, not just schema. Both the player and enemies are instances
// of the same `Actor` struct in a single pool. Every system that
// touches actors (animation tick, hip-delta apply, collision,
// hurtbox build, damage application, hit reactions) iterates the
// pool once and acts on every actor symmetrically.
//
// Behavior differs by `Controller` tag — Input for the player, AI
// for enemies. Systems below the controller layer (sampler update,
// hip-delta apply, collision, etc.) don't care which controller
// drives the actor; they're actor-agnostic by construction.
//
// See docs/design/pc-vs-npc.md for the design rule and
// docs/design/bestiary.md for its bestiary-side consequence
// (figura umana — one rig, deformed by sin).

// Who an actor is hostile/friendly to. Damage application checks
// faction pairs to decide if a hit applies. Start with a small
// enum; widen to a hostility matrix only when the game demands it.
enum class Faction
{
    Player,  // the PC; hostile to Hostile-faction actors
    Hostile, // damned souls, demons; hostile to Player
    Neutral, // can be attacked but doesn't attack first (NPCs in lucid window)
    Allied,  // friendly to Player (the Guide; future companions)
};

// Returns true if `attacker` should damage `target` on hit.
constexpr bool factionsHostile(Faction attacker, Faction target)
{
    if (attacker == Faction::Player && target == Faction::Hostile)
        return true;
    if (attacker == Faction::Hostile && (target == Faction::Player || target == Faction::Allied))
        return true;
    return false;
}

// Mortal pool. Current drops when damaged; max is derived from
// Body::base_hp + Stats::vig scaling. HP <= 0 marks the actor for
// death cleanup.
struct Health
{
    int current = 100;
    int max = 100;
};

// Action pool. Drains on attack / dodge / sprint; regenerates while
// not committing to actions. Recovery curves live in the tunables
// JSON later.
struct Stamina
{
    int current = 100;
    int max = 100;
};

// Stagger reservoir. Souls-convention: starts at max, drains by
// per-attack poise_damage on each hit. When current hits 0, the
// hit triggers a knockdown chain (knockdown clip → getting_up clip)
// and poise resets to max. While not taking hits, poise refills
// (full refill after poise_decay_window_seconds of no damage).
//
// Poise breakability is what defines combat weight: hits that don't
// break poise are "absorbed" (light flinch / hit-react); hits that
// break it commit the target to a knockdown — the whole-body
// commitment that separates "tagged" from "staggered."
struct Poise
{
    int current = 100;
    int max = 100;
    // Wallclock time of the last poise-damage event. Drives the
    // decay-window refill timer. -1 = never hit.
    float last_damage_time = -1.0f;
};

// Souls-convention quad. Other stats (faith / intellect for
// magic-equivalent systems) slot in when those systems arrive; the
// schema is intentionally open.
struct Stats
{
    int vig = 10; // Vigor:     scales max HP
    int end = 10; // Endurance: scales max stamina + equip load
    int str = 10; // Strength:  scales STR-weapon damage, gates heavy weapons
    int dex = 10; // Dexterity: scales DEX-weapon damage, gates light weapons
};

// Per-archetype "what species are you" properties. Not leveled.
// Distinguishes a hulking glutton-shade from a gaunt heretic-shade
// before per-instance stats stack on top. For the player today this
// holds the same humanoid defaults; tomorrow's class system layers
// onto Stats, not Body.
struct Body
{
    int base_hp = 50;
    int base_stamina = 80;
    int base_poise = 30;
    int base_defense = 0; // flat damage reduction; clamped to >= 1 incoming
    float unarmed_damage = 6.0f;
    float unarmed_poise_damage = 8.0f; // baseline poise damage per fist hit
    float collider_radius = 0.35f;     // XZ capsule radius for collision
};

// Derived max HP / stamina / poise from Body + Stats. Soft linear
// scaling for v1 — coefficients live in Tunables (hp_per_vig,
// stamina_per_end, poise_per_end, poise_per_str) so they hot-reload
// via the F1 panel. Souls-style diminishing curves replace these
// when balance work begins; call sites don't change.
int computeMaxHp(const Body& body, const Stats& stats);
int computeMaxStamina(const Body& body, const Stats& stats);
int computeMaxPoise(const Body& body, const Stats& stats);

// Initialize the actor's mortal + action + stagger pools to full
// from the archetype Body + Stats. Call once at spawn; thereafter
// `current` changes through gameplay (damage, regen) while `max`
// stays put until stats change (level-up later).
void initActorPools(Health& hp, Stamina& stamina, Poise& poise, const Body& body,
                    const Stats& stats);

// Apply raw incoming damage to `hp`, mediated by `body.base_defense`.
// Damage is clamped to at least 1 so even heavily-armored targets
// take a chip on every hit. Health::current floors at 0 — death is
// detected separately by a caller checking current <= 0.
void applyDamage(Health& hp, const Body& body, int raw_damage);

// Pure damage computation for an attack. base / str_scale / dex_scale
// come from the weapon (or unarmed profile on the attacker's Body).
// Returns an integer damage value to feed into applyDamage().
//
// Linear scaling for v1, matching prison-escape's pattern. The
// Souls-style diminishing curve replaces this when balance work
// begins — the call site doesn't change, only the function body.
int computeAttackDamage(const Stats& attacker, float base, float str_scale, float dex_scale);

// What kind of intent driver an actor uses. Each per-frame tick
// reads `actor.controller` and dispatches to the correct intent
// source (input + camera for Input, AI for AI tags, none for
// Corpse). All systems below the dispatch layer are controller-
// agnostic.
enum class Controller : std::uint8_t
{
    Input,         // the PC; intent from keyboard / mouse
    AI_Stationary, // placeholder enemies: idle, no AI yet
    Corpse,        // dead actor; no intent, holds death pose
};

// One actor. Player + every enemy is an Actor instance in the
// shared pool. The Controller tag is the ONLY thing that
// differentiates them at the per-frame system level — schemas,
// animation, collision, combat are all identical.
struct Actor
{
    // --- Schema (same shape on player + enemy) ---
    Health hp;
    Stamina stamina;
    Poise poise;
    Stats stats;
    Body body;
    Faction faction = Faction::Hostile;

    // --- Transform / motion ---
    glm::vec3 pos = glm::vec3(0.0f);
    float yaw = 0.0f;
    glm::vec2 velocity_xz = glm::vec2(0.0f);

    // --- Animation ---
    // Per-actor pose state. Each actor's clips advance in their
    // own sampler, blended against the shared skeleton + mesh.
    selva::anim::PoseSampler sampler;

    // --- Controller / behavior ---
    Controller controller = Controller::AI_Stationary;

    // --- Per-controller intent flags ---
    // Latched intent: sprinting (Input controller only). Future
    // controllers may add their own intent fields here.
    bool sprinting = false;

    // Lock-on target index into actors() (Input controller only).
    // -1 = unlocked; >=0 = combat mode: yaw snaps to target each frame,
    // WASD becomes target-relative, camera derives from player↔target
    // axis, idle picks unarmed_combat_idle, locomotion picks 1 of 4
    // directional combat clips. Cleared by middle-mouse toggle, by
    // sprint engaging, or when the target dies. Index-based (not
    // pointer) so it survives push_back reallocations during enemy
    // spawn — actors() is a std::vector and pointers into it are not
    // stable across grow.
    int lock_target_idx = -1;

    // Circle-strafe direction for AI duel mode. 0 = no committed
    // side (set on lock acquire to ±1 via actor.rng). +1 = strafe to
    // the target's right (player's left); -1 = strafe to the target's
    // left. Sticky for the duration of the engagement so the AI
    // doesn't flip sides every frame. Player ignores this field.
    int duel_strafe_dir = 0;

    // --- Combat reaction state ---
    // Wallclock time of last damage event. Drives in-world HP bar
    // visibility. -1 = never damaged.
    float last_damage_time = -1.0f;
    // Wallclock time of last hit-react fire. Drives the cooldown
    // gate so rapid multi-hits don't restart the animation every
    // frame.
    float last_hit_react_time = -1.0f;

    // --- Death state ---
    // True from the moment the death one-shot fires. Subsequent
    // hits become no-ops; the body holds the death pose. Cleared
    // by the dev respawn timer when it elapses.
    bool is_dead = false;
    float death_time = -1.0f;

    // --- Knockdown state ---
    // Set true when a hit breaks poise and the knockdown clip fires.
    // The actor is hit-immune while down; the knockdown clip freezes
    // on its last frame; tickActors clears is_knocked_down after
    // `enemy_recovery_after_knockdown_seconds` and the sampler blends
    // back to combat idle in place.
    bool is_knocked_down = false;
    float knockdown_start_time = -1.0f;

    // --- Spawn pose (for respawn) ---
    glm::vec3 spawn_pos = glm::vec3(0.0f);
    float spawn_yaw = 0.0f;

    // --- AI perception state ---
    // Updated by tickPerception each frame. Behavior tree (future)
    // and locomotion-intent (future) read awareness + last-known-
    // player-pos from here.
    PerceptionState perception;

    // --- AI scheduler ---
    // Wallclock time of the next scheduled decision tick. Behavior
    // tree (future) only re-evaluates when wallClock() >= this.
    // Spawned with a phase offset so a wave of actors doesn't all
    // tick on the same frame. See gameplay/AiTick.h.
    float next_ai_tick_time = 0.0f;

    // --- AI archetype binding ---
    // Pointer to the loaded archetype data (action list, perception
    // overrides). nullptr = use defaults (test-dummy fallback).
    // Sprint 4 will read actions[] here to drive the behavior tree.
    const struct EnemyArchetype* archetype = nullptr;

    // --- AI locomotion intent (Sprint 4a) ---
    // Written by the decision tick (tickEnemyDecision) and consumed
    // every frame by tickEnemyLocomotion. Mirrors the player's
    // input-driven moveIntent so the locomotion path stays
    // controller-agnostic.
    glm::vec2 intent_xz = glm::vec2(0.0f); // XZ target direction × speed
    float turn_intent_yaw = 0.0f;          // yaw the actor wants to face

    // Per-action runtime state — cooldown timestamps for each
    // archetype-declared action. Lazy: actions are inserted on first
    // lookup (LeafPickAction). Keyed by EnemyAction::id from the
    // archetype JSON. Souls-style cooldowns keep weighted-random
    // selection from spamming the strongest action.
    struct ActionRuntime
    {
        float cooldown_until_time = 0.0f; // wallclock; can fire when now >= this
    };
    std::unordered_map<std::string, ActionRuntime> action_state;

    // Per-actor RNG for weighted-random action picks. Seeded at
    // spawn from std::random_device — different actors of the same
    // archetype roll independently so they don't synchronize.
    std::mt19937 rng;

    // --- Active attack hitbox tracking ---
    // When the actor fires a swing, this stores the spawned hitbox's
    // id + the bone joint that drives its world position + the tip
    // offset along the joint's forward axis. Per-frame, the
    // tickActiveAttackHitboxes pass re-anchors the hitbox to the
    // joint's current pose so the volume tracks the swinging hand.
    // 0 = no active hitbox. The hitbox-update path is the SAME for
    // PC and NPC — both attacking actors get the same treatment.
    // Without this tracking, the hitbox is frozen at the spawn-
    // frame joint position (usually a windup pose with the hand at
    // the hip) — visually the swing extends through space but the
    // hitbox sits behind the attacker. Bug surfaced when AI enemies
    // started attacking and didn't land their hits.
    std::uint32_t active_attack_hitbox_id = 0;
    int active_attack_joint_idx = -1;
    float active_attack_tip_offset_z = 0.0f;
};

// Apply the actor's sampler-consumed hip-XZ delta to its world
// position. THE one place this math lives — every caller (player
// path, enemy tick, anywhere else translating an actor by clip-
// authored hip motion) routes through here so the contract is
// unified. `hip_delta_scale` lets the caller scale the applied
// translation (used for the walking-jump variant where authored
// clip travel is scaled to 55% so the same clip covers shorter
// distance at the same playback rate).
void applyActorClipHipDelta(Actor& actor, float hip_delta_scale = 1.0f);

// Resolve `actor.lock_target_idx` to a pointer into actors(), or
// nullptr if unlocked / index stale. Pool can reallocate on enemy
// spawn so callers must re-resolve per-frame rather than caching.
// Used by both PC (lock-on combat mode) and AI (engagement target).
Actor* resolveLockTarget(const Actor& actor);

// Choose a directional locomotion clip given a facing basis and
// movement intent. Shared between PC (lock-on combat mode) and AI
// (engagement strafe). Caller supplies the facing fwd / right
// vectors (already-normalized XZ unit vectors) plus the world-frame
// intent vector. `running` selects between walking and running
// clip families. Returns nullptr if intent is effectively zero.
// Strafe wins any nonzero lateral input — diagonals are strafes.
const char* directionalLocoClip(const glm::vec3& fwd, const glm::vec3& right,
                                const glm::vec3& intent, bool running);

// Reparent the actor's active attack hitbox (if any) to the bone
// joint that drives it, using the current pose. Called per-frame
// for every actor that might have an active swing. Without this
// the hitbox stays frozen at its spawn-frame position (the windup
// pose, hand at hip) and the swing visually arcs through space
// without the volume tracking — visible as "AI punches don't
// connect." Works identically for PC and NPC; takes the place of
// the historical sActiveAttackHitboxId/...joint-idx file-statics
// that the PC's PerFrameTick maintained.
void updateActiveAttackHitbox(Actor& actor);

// The actor pool. Player is conventionally at index 0; enemies
// follow at 1..N. Future systems (companions, NPCs, projectiles)
// join the same pool.
std::vector<Actor>& actors();

// Convenience accessor for the player. Returns actors()[0].
// Assumes the pool has at least one entry — call after the
// player has been initialized.
Actor& player();

// Initialize the pool with the player at index 0. Called once at
// startup, before any gameplay tick. Idempotent: clears + spawns.
void initActorPool();

// Per-frame tick driving actor-agnostic systems. Iterates the
// pool, advances each actor's animation, applies the consumed
// hip delta to world position (the contract that keeps feet
// planted, see feedback_hip_delta_two_sides.md), then applies
// actor-vs-world + actor-vs-actor collision push-out.
void tickActors(float dt);

} // namespace selva::gameplay
