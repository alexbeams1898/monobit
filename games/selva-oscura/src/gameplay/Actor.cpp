#include "gameplay/Actor.h"

#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/SkeletalAssets.h"

#include <algorithm>
#include <cmath>

namespace selva::gameplay
{

int computeMaxHp(const Body& body, const Stats& stats)
{
    const auto& tun = selva::tuning::current();
    return body.base_hp + static_cast<int>(std::floor(static_cast<float>(stats.vig) * tun.hp_per_vig));
}

int computeMaxStamina(const Body& body, const Stats& stats)
{
    const auto& tun = selva::tuning::current();
    return body.base_stamina +
           static_cast<int>(std::floor(static_cast<float>(stats.end) * tun.stamina_per_end));
}

void initActorPools(Health& hp, Stamina& stamina, const Body& body, const Stats& stats)
{
    hp.max = computeMaxHp(body, stats);
    hp.current = hp.max;
    stamina.max = computeMaxStamina(body, stats);
    stamina.current = stamina.max;
}

void applyDamage(Health& hp, const Body& body, int raw_damage)
{
    if (raw_damage <= 0)
        return;
    const auto& tun = selva::tuning::current();
    const int floor_damage = std::max(1, static_cast<int>(std::floor(tun.damage_floor)));
    const int after_defense = std::max(floor_damage, raw_damage - body.base_defense);
    hp.current = std::max(0, hp.current - after_defense);
}

int computeAttackDamage(const Stats& attacker, float base, float str_scale, float dex_scale)
{
    const float str_bonus = std::floor(static_cast<float>(attacker.str) * str_scale);
    const float dex_bonus = std::floor(static_cast<float>(attacker.dex) * dex_scale);
    const float total = base + str_bonus + dex_bonus;
    if (total <= 0.0f)
        return 0;
    return static_cast<int>(total);
}

void applyActorClipHipDelta(Actor& actor, float hip_delta_scale)
{
    // Contract from feedback_hip_delta_two_sides.md: PoseSampler
    // extracts the clip's authored hip-XZ and zeros it in the local
    // pose. Gameplay must apply that consumed delta back to world
    // position or the actor's feet treadmill. Caller decides WHEN
    // to call this (always for AI actors; only during one-shots
    // for the Input-controlled player whose locomotion is velocity-
    // driven instead).
    const glm::vec3 hip_local = actor.sampler.consumedHipDelta();
    if (std::abs(hip_local.x) <= 1e-6f && std::abs(hip_local.z) <= 1e-6f)
        return;
    const float sy = std::sin(actor.yaw);
    const float cy = std::cos(actor.yaw);
    const glm::vec3 hip_world(-cy * hip_local.x - sy * hip_local.z, 0.0f,
                              sy * hip_local.x - cy * hip_local.z);
    actor.pos += hip_world * hip_delta_scale;
}

// ---- Pool, accessors, and tick ----

namespace
{
std::vector<Actor> sActors;
} // namespace

std::vector<Actor>& actors()
{
    return sActors;
}

Actor& player()
{
    // Player is conventionally at index 0; initActorPool spawns it
    // first. Calling this before initActorPool is a logic error
    // (returns the back-compat shim instead of crashing). When the
    // shim is removed and the pool is the only state, this becomes
    // a hard precondition.
    return sActors.front();
}

void initActorPool()
{
    sActors.clear();
    Actor pc;
    pc.controller = Controller::Input;
    pc.faction = Faction::Player;
    initActorPools(pc.hp, pc.stamina, pc.body, pc.stats);
    // Player's sampler is bound to the shared skeleton + mesh at
    // first sampler.update() — same as any other actor. Pre-warm
    // it so the bone palette is valid before render.
    pc.sampler =
        selva::anim::createPoseSampler(selva::anim::skeleton(), selva::anim::playerMesh());
    if (const auto* idle = selva::anim::idleClip(); idle != nullptr && idle->isLoaded())
        pc.sampler.update(*idle, 0.0f, 0.0f);
    sActors.push_back(std::move(pc));
}

void tickActors(float dt)
{
    // Actor-agnostic per-frame systems. Iterates every actor in the
    // pool and applies the systems that have unified contracts.
    //
    // Today: hip-delta apply (the contract from
    // feedback_hip_delta_two_sides.md — extraction zeros hip-XZ in
    // the local pose; gameplay must apply it as world translation
    // or feet treadmill).
    //
    // The PLAYER (controller=Input) is intentionally SKIPPED here:
    // its translation has a dual-path gating system (velocity for
    // locomotion, clip-hip for one-shots, mediated by
    // movement_locked) that's still in PerFrameTick. Applying hip
    // delta here would double-translate during locomotion. When
    // that gating logic migrates into this function, the skip
    // disappears and ALL actors run the same hip-delta apply.
    //
    // Dead actors run hip-delta the same as alive — the death clip
    // authors the body falling forward, and that motion must reach
    // world space so the corpse lands at its true location rather
    // than holding the death animation in place.
    for (auto& a : sActors)
    {
        if (a.controller == Controller::Input)
            continue;
        applyActorClipHipDelta(a);
    }
    (void)dt;
}

} // namespace selva::gameplay
