#include "combat/ActorVolumes.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace selva::combat
{

namespace
{

glm::vec3 jointWorld(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                     const char* name)
{
    const int idx = sampler.findJoint(name);
    if (idx < 0)
        return glm::vec3(actor_model[3]); // actor origin if joint missing
    const glm::vec3 local = sampler.jointWorldPos(idx);
    const glm::vec4 world = actor_model * glm::vec4(local, 1.0f);
    return glm::vec3(world);
}

// Build a region capsule between two joints. radius scales with
// the actor's collider_radius so larger bodies have larger
// hurtboxes without per-region tuning data yet.
Hurtbox capsuleBetween(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                       const char* joint_a, const char* joint_b, HurtRegion region, float radius,
                       OwnerRef owner, selva::gameplay::Faction faction)
{
    Hurtbox h;
    h.shape.p0 = jointWorld(sampler, actor_model, joint_a);
    h.shape.p1 = jointWorld(sampler, actor_model, joint_b);
    h.shape.radius = radius;
    h.region = region;
    h.owner = owner;
    h.faction = faction;
    return h;
}

} // namespace

glm::mat4 buildActorModelMatrix(const glm::vec3& pos, float yaw, float foot_offset_y)
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, -foot_offset_y, pos.z));
    return glm::rotate(m, yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void appendActorHurtboxes(const selva::anim::PoseSampler& sampler, const glm::mat4& actor_model,
                          const selva::gameplay::Body& body, OwnerRef owner,
                          selva::gameplay::Faction faction)
{
    if (sampler.jointCount() <= 0)
        return;
    const float base_r = body.collider_radius;
    auto& pool = hurtboxes();

    // Torso: hips to neck. Largest hurtbox.
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:Hips", "mixamorig:Neck",
                                  HurtRegion::Torso, base_r * 0.9f, owner, faction));
    // Head: neck to head joint. Smaller radius, headshot multiplier
    // will live on per-region damage when authoring lands.
    Hurtbox head = capsuleBetween(sampler, actor_model, "mixamorig:Neck", "mixamorig:Head",
                                  HurtRegion::Head, base_r * 0.55f, owner, faction);
    head.damage_multiplier = 1.5f;
    pool.push_back(head);

    // Upper limbs (4 capsules: each upper arm + each thigh).
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:LeftArm",
                                  "mixamorig:LeftForeArm", HurtRegion::UpperLimb, base_r * 0.35f,
                                  owner, faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:RightArm",
                                  "mixamorig:RightForeArm", HurtRegion::UpperLimb, base_r * 0.35f,
                                  owner, faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:LeftUpLeg",
                                  "mixamorig:LeftLeg", HurtRegion::UpperLimb, base_r * 0.5f, owner,
                                  faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:RightUpLeg",
                                  "mixamorig:RightLeg", HurtRegion::UpperLimb, base_r * 0.5f, owner,
                                  faction));

    // Lower limbs (forearms + shins).
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:LeftForeArm",
                                  "mixamorig:LeftHand", HurtRegion::LowerLimb, base_r * 0.3f, owner,
                                  faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:RightForeArm",
                                  "mixamorig:RightHand", HurtRegion::LowerLimb, base_r * 0.3f,
                                  owner, faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:LeftLeg",
                                  "mixamorig:LeftFoot", HurtRegion::LowerLimb, base_r * 0.4f, owner,
                                  faction));
    pool.push_back(capsuleBetween(sampler, actor_model, "mixamorig:RightLeg",
                                  "mixamorig:RightFoot", HurtRegion::LowerLimb, base_r * 0.4f,
                                  owner, faction));
}

} // namespace selva::combat
