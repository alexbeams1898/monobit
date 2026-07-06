#pragma once

#include "anim/PoseSampler.h"
#include "combat/HitVolumes.h"
#include "gameplay/Actor.h"

#include <glm/mat4x4.hpp>

namespace selva::combat
{

// Build the actor-to-world transform for a character whose origin
// is at (pos.x, -foot_offset_y * body_scale, pos.z), faces +yaw with
// the 180-degree bind offset, and renders at body_scale of the
// bind-pose size. body_scale=1.0 reproduces the prior behavior
// exactly. Mirrors what the renderer builds; centralized here so
// hitbox + hurtbox + render all agree on the joint -> world mapping
// AND on the visual size.
//
// Why body_scale lives on this matrix (not on a separate uniform):
// the renderer + every hit/hurt volume callsite uses this single
// transform to map model-space joints to world space. Scaling once
// here means the body, its hurtboxes, its hitboxes, and the
// equipped-weapon attachment all stay locked together at any size.
// Adding it as a separate "scale uniform" downstream would split the
// source of truth.
glm::mat4 buildActorModelMatrix(const glm::vec3& pos, float yaw, float foot_offset_y,
                                float body_scale);

// Populate per-actor hurtboxes from joint world positions. Appends
// to the global hurtbox pool — clearHurtboxes() must have been
// called this frame first. Iterates body.hurtbox_decls (authored
// per skeleton); empty decls = actor takes no hits (intentional or
// misconfigured).
void appendActorHurtboxes(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                          const selva::gameplay::Body& body, OwnerRef owner,
                          selva::gameplay::Faction faction);

// Parse a hurtbox decl list from a JSON file. Schema:
//   { "hurtboxes": [
//       { "joint_a": "...", "joint_b": "...", "region": "Head|Torso|UpperLimb|LowerLimb",
//         "radius_scale": 0.55, "damage_multiplier": 1.5 }
//   ] }
// Returns empty vector if the file is missing / malformed; caller
// inspects empty + logs. Used at boot to populate Body.hurtbox_decls
// for the player (config/skeletons/humanoid_male_hurtboxes.json) and for
// each enemy archetype via its archetype JSON.
std::vector<HurtboxDecl> loadHurtboxDecls(const char* json_path);

} // namespace selva::combat
