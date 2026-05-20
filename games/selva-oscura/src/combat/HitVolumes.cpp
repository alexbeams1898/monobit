#include "combat/HitVolumes.h"

#include "anim/PoseSampler.h"
#include "combat/ActorVolumes.h"
#include "gameplay/Actor.h"

#include <algorithm>

namespace selva::combat
{

namespace
{
std::vector<Hurtbox> sHurtboxes;
std::vector<Hitbox> sHitboxes;
std::uint32_t sNextHitboxId = 1;
} // namespace

std::vector<Hurtbox>& hurtboxes()
{
    return sHurtboxes;
}

std::vector<Hitbox>& hitboxes()
{
    return sHitboxes;
}

void clearHurtboxes()
{
    sHurtboxes.clear();
}

void tickHitboxes(float dt)
{
    for (auto& hb : sHitboxes)
    {
        hb.prev_shape = hb.shape;
        hb.has_prev = true;
        hb.remaining_seconds -= dt;
    }
    sHitboxes.erase(std::remove_if(sHitboxes.begin(), sHitboxes.end(),
                                   [](const Hitbox& h) { return h.remaining_seconds <= 0.0f; }),
                    sHitboxes.end());
}

std::uint32_t spawnHitbox(const Hitbox& proto)
{
    Hitbox h = proto;
    h.id = sNextHitboxId++;
    h.prev_shape = h.shape;
    h.has_prev = false;
    sHitboxes.push_back(h);
    return h.id;
}

Hitbox* findHitbox(std::uint32_t id)
{
    for (auto& h : sHitboxes)
    {
        if (h.id == id)
            return &h;
    }
    return nullptr;
}

std::uint32_t spawnAttackHitbox(const AttackHitboxSpawnParams& p)
{
    if (p.actor == nullptr || p.joint_name == nullptr)
        return 0;
    const int joint_idx = p.actor->sampler.findJoint(p.joint_name);
    if (joint_idx < 0)
        return 0;
    const float rate = (p.playback_rate > 0.0f) ? p.playback_rate : 1.0f;
    const float effective_dur =
        std::max(0.0f, (p.clip_duration_seconds - p.clip_start_seconds) / rate);
    const float lifetime = std::max(0.10f, effective_dur * p.lifetime_fraction);
    const glm::mat4 model_mat =
        buildActorModelMatrix(p.actor->pos, p.actor->yaw, p.mesh_foot_offset_y);
    const glm::vec3 anchor_world =
        glm::vec3(model_mat * glm::vec4(p.actor->sampler.jointWorldPos(joint_idx), 1.0f));
    // Weapon hitboxes extend along the joint's forward axis. For a
    // fist (tip_offset_z = 0) this collapses to a sphere at the
    // joint. For a sword we'd add ~0.8m along the hand's forward
    // to span hilt → tip.
    const glm::vec3 tip_world =
        (p.hitbox_tip_offset_z != 0.0f)
            ? anchor_world +
                  glm::vec3(model_mat * glm::vec4(0.0f, 0.0f, p.hitbox_tip_offset_z, 0.0f))
            : anchor_world;
    Hitbox proto;
    proto.shape.p0 = anchor_world;
    proto.shape.p1 = tip_world;
    proto.shape.radius = p.hitbox_radius;
    proto.attacker = p.attacker;
    proto.attacker_faction = p.attacker_faction;
    proto.raw_damage = p.raw_damage;
    proto.poise_damage = p.poise_damage;
    proto.remaining_seconds = lifetime;
    const std::uint32_t id = spawnHitbox(proto);
    // Record on the actor so updateActiveAttackHitbox can re-anchor
    // the capsule to the joint each frame as the swing progresses.
    // The PC's PerFrameTick used file-static fields; lifting onto
    // Actor unifies PC + NPC. Cast away const for this write — the
    // params are otherwise read-only and pass-through.
    auto* actor_mut = const_cast<selva::gameplay::Actor*>(p.actor);
    actor_mut->active_attack_hitbox_id = id;
    actor_mut->active_attack_joint_idx = joint_idx;
    actor_mut->active_attack_tip_offset_z = p.hitbox_tip_offset_z;
    return id;
}

} // namespace selva::combat
