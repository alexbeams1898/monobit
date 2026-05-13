#pragma once

#include "anim/PoseSampler.h"
#include "gameplay/Perception.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
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
