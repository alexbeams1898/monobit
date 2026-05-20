#pragma once

#include "gameplay/Actor.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace selva::combat
{

// Capsule-based hit volumes — the Souls-series convention.
//
// HURTBOX: capsule attached to a character's bone. Multiple per
// actor for region damage multipliers (head/torso/limbs). Damage
// applied to the owning actor when a hostile hitbox overlaps.
//
// HITBOX: swept capsule on a weapon/limb. Active for a window of
// the attack clip's wallclock time. Damages hurtboxes of hostile
// faction only.
//
// Both are reset every frame, populated from animation state +
// per-attack authoring data, and queried for overlap before
// rendering.

// Owner reference. PoolKind disambiguates between the player and
// an enemy index — no global entity ID system yet, so volume
// owners are addressed by their pool.
enum class OwnerKind : std::uint8_t
{
    Player = 0,
    Enemy = 1,
};

struct OwnerRef
{
    OwnerKind kind = OwnerKind::Player;
    int index = 0; // for OwnerKind::Enemy, index into Enemies pool

    bool operator==(const OwnerRef& o) const
    {
        return kind == o.kind && index == o.index;
    }
};

// Capsule: line segment from p0 to p1 thickened by radius. World
// space. A radius-only sphere is a capsule with p0 == p1.
struct Capsule
{
    glm::vec3 p0{0.0f};
    glm::vec3 p1{0.0f};
    float radius = 0.10f;
};

// Body region the hurtbox covers. Drives the damage multiplier
// when this region takes a hit (head crits, limbs less).
enum class HurtRegion : std::uint8_t
{
    Head,
    Torso,
    UpperLimb,
    LowerLimb,
};

struct Hurtbox
{
    Capsule shape;
    OwnerRef owner;
    selva::gameplay::Faction faction = selva::gameplay::Faction::Player;
    HurtRegion region = HurtRegion::Torso;
    float damage_multiplier = 1.0f; // overrides region default if non-1
};

struct Hitbox
{
    Capsule shape;
    Capsule prev_shape; // last-frame position for swept detection
    OwnerRef attacker;
    selva::gameplay::Faction attacker_faction = selva::gameplay::Faction::Player;
    int raw_damage = 0;
    int poise_damage = 0;
    // Lifetime: hitbox expires when remaining_seconds <= 0.
    float remaining_seconds = 0.0f;
    // Unique-ish ID per hitbox instance so a single swing doesn't
    // double-hit the same hurtbox owner across multiple frames.
    // Hurtbox-owner-pair recorded in HitDetection until the hitbox
    // expires.
    std::uint32_t id = 0;
    bool has_prev = false; // false on the first tick — prev is undefined
};

// Per-frame pools. Both are cleared and repopulated each frame
// (hurtboxes from bone transforms; hitboxes survive across frames
// but get their `prev_shape` updated). Linear scan for overlap is
// fine until counts exceed ~50 (Souls-class).
std::vector<Hurtbox>& hurtboxes();
std::vector<Hitbox>& hitboxes();

// Clear hurtboxes (per-frame rebuild). Hitboxes persist across
// frames; their lifetime decremented in tickHitboxes.
void clearHurtboxes();

// Advance hitbox lifetimes by dt; despawn expired. Also rolls
// `shape` into `prev_shape` so the NEXT frame's overlap test is
// swept against this frame's position.
void tickHitboxes(float dt);

// Spawn a hitbox. Returns its id for tracking. The caller is
// responsible for keeping `shape` current (parenting to a joint
// each frame) until lifetime expires.
std::uint32_t spawnHitbox(const Hitbox& proto);

// Look up an active hitbox by id so animation code can update its
// shape each frame. Returns nullptr if expired/missing.
Hitbox* findHitbox(std::uint32_t id);

// Bundle of inputs to spawn an attack hitbox parented to a joint
// on an actor. Used by both the PC's fireClipForHand and the NPC's
// LeafPickAction — single source of truth for the geometry math
// (joint world-space lookup, model matrix transform, swept tip
// extension) and lifetime computation. Per the PC/NPC symmetry
// rule (docs/design/pc-vs-npc.md) the spawn-side logic is one
// function, not two parallel copies.
struct AttackHitboxSpawnParams
{
    const selva::gameplay::Actor* actor = nullptr;
    OwnerRef attacker;
    selva::gameplay::Faction attacker_faction = selva::gameplay::Faction::Player;
    int raw_damage = 0;
    int poise_damage = 0;
    // Hitbox geometry on the actor's skeleton.
    const char* joint_name = nullptr;
    float hitbox_radius = 0.18f;
    float hitbox_tip_offset_z = 0.0f;
    // Lifetime control: clip duration + per-actor playback rate
    // determine the active window. Default 0.55x of the clip's
    // wallclock duration past the start_seconds offset.
    float clip_duration_seconds = 0.0f;
    float clip_start_seconds = 0.0f;
    float playback_rate = 1.0f;
    float lifetime_fraction = 0.55f; // fraction of effective duration the hitbox is active
    float mesh_foot_offset_y = 0.0f; // for buildActorModelMatrix
};

// Spawn an attack hitbox using the geometry math common to both
// player and enemy attacks. Returns the spawned hitbox id (0 on
// failure — joint not found, lifetime zero, etc.). Caller does not
// need to update the hitbox per-frame; tickHitboxes ages it out.
std::uint32_t spawnAttackHitbox(const AttackHitboxSpawnParams& p);

} // namespace selva::combat
