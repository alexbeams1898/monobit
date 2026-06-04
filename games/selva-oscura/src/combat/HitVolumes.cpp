#include "combat/HitVolumes.h"

#include "anim/PoseSampler.h"
#include "combat/ActorVolumes.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"

#include <algorithm>
#include <cmath>

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
    // Decay + expire only. prev_shape is updated inside
    // Actor::updateActiveAttackHitbox right BEFORE the new per-frame
    // shape is written -- doing it here would have raced with
    // detectHits (tickHitboxes runs before detectHits, so this
    // function used to copy curr -> prev and collapse the swept
    // capsule to a point).
    for (auto& hb : sHitboxes)
    {
        hb.remaining_seconds -= dt;
        const selva::gameplay::Actor* owner = nullptr;
        if (hb.attacker.kind == OwnerKind::Player)
            owner = &selva::gameplay::player();
        else
            owner = selva::gameplay::enemyAt(hb.attacker.index);
        if (owner == nullptr || !selva::gameplay::actorCanLandHits(*owner))
            hb.remaining_seconds = 0.0f;
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
    // Geometry diagnostic for enemy hit-reach tuning. Logs the
    // distance from the spawn anchor (joint world pos) to the
    // PLAYER's center at spawn time so authoring can see whether
    // range_max is too generous relative to actual reach. Filtered
    // to enemy attackers (the player's own swings have their own
    // diagnostics elsewhere). Read with [hitbox-spawn] tag in the
    // combat log.
    if (p.attacker.kind == selva::combat::OwnerKind::Enemy)
    {
        const glm::vec3 pc_pos = selva::gameplay::player().pos;
        const float dx = anchor_world.x - pc_pos.x;
        const float dy = anchor_world.y - pc_pos.y;
        const float dz = anchor_world.z - pc_pos.z;
        const float anchor_to_pc_3d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float anchor_to_pc_xz = std::sqrt(dx * dx + dz * dz);
        const float actor_to_pc_xz =
            std::sqrt((p.actor->pos.x - pc_pos.x) * (p.actor->pos.x - pc_pos.x) +
                      (p.actor->pos.z - pc_pos.z) * (p.actor->pos.z - pc_pos.z));
        selva::combat::combatLog("[hitbox-spawn] joint={} radius={:.2f} actor_xz_to_pc={:.2f} "
                                 "anchor_xz_to_pc={:.2f} anchor_3d_to_pc={:.2f}",
                                 p.joint_name, p.hitbox_radius, actor_to_pc_xz, anchor_to_pc_xz,
                                 anchor_to_pc_3d);
    }
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
