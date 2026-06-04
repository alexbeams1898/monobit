#pragma once

#include <cstdint>
#include <string>

namespace selva::combat
{

// Body region the hurtbox covers. Drives the damage multiplier
// when this region takes a hit (head crits, limbs less).
enum class HurtRegion : std::uint8_t
{
    Head,
    Torso,
    UpperLimb,
    LowerLimb,
};

// Declarative per-actor hurtbox layout. Each entry becomes one
// capsule between two skeleton joints with the given radius scale
// (multiplied by the actor's body.collider_radius at build time)
// and an optional damage multiplier (e.g. 1.5 for head). Authored
// per skeleton: player has its own player_hurtboxes.json; each
// enemy archetype JSON carries its own hurtboxes array. PoseSampler-
// queried joint names are the skeleton's actual joint names (NOT
// semantic roles -- the author knows the rig they're writing for).
//
// Lives in its own header so gameplay/Actor.h::Body can hold a
// vector of HurtboxDecl without dragging in combat/HitVolumes.h
// (which transitively includes Actor.h -- circular).
struct HurtboxDecl
{
    std::string joint_a;
    std::string joint_b;
    HurtRegion region = HurtRegion::Torso;
    float radius_scale = 0.5f;      // multiplied by Body.collider_radius
    float damage_multiplier = 1.0f; // overrides region default if non-1
};

} // namespace selva::combat
