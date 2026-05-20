#pragma once

#include "anim/PoseSampler.h"
#include "combat/HitVolumes.h"
#include "gameplay/Actor.h"

#include <glm/mat4x4.hpp>

namespace selva::combat
{

// Build the actor-to-world transform for a character whose origin
// is at (pos.x, -foot_offset_y, pos.z) and which faces +yaw with
// the Mixamo 180-degree bind offset. Mirrors what the renderer
// builds; centralized here so hitbox + hurtbox + render all agree
// on the joint -> world mapping.
glm::mat4 buildActorModelMatrix(const glm::vec3& pos, float yaw, float foot_offset_y);

// Populate per-actor hurtboxes from joint world positions. Appends
// to the global hurtbox pool — clearHurtboxes() must have been
// called this frame first.
void appendActorHurtboxes(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                          const selva::gameplay::Body& body, OwnerRef owner,
                          selva::gameplay::Faction faction);

} // namespace selva::combat
