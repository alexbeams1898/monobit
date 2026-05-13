#include "gameplay/Enemies.h"

#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "combat/CombatLog.h"
#include "gameplay/AiTick.h"
#include "gameplay/Perception.h"
#include "world/Collision.h"

#include <algorithm>
#include <cmath>

namespace selva::gameplay
{

namespace
{

// Severity tier + cooldown + respawn knobs live in Tunables so
// they're hot-reloadable from the F1 panel and serialize with the
// rest of the game's feel parameters. Read fresh each call.

// Generic hit-react clip set, baked from
// games/selva-oscura/assets/characters/x_bot/source/combat/unarmed/.
// All authored on the shared X_Bot rig; the figura umana rule means
// every enemy in the bestiary uses these same clips.
//
// Enemies idle in combat stance — flinch / hit-react clips were
// authored against this pose. Using standard_idle produces visual
// seams at the hip when blending. Hostile enemies are always in
// combat stance.
constexpr const char* kEnemyIdleClipName = "unarmed_combat_idle";
constexpr const char* kFlinchFrontClipName = "flinch_front";
constexpr const char* kFlinchBackClipName = "flinch_back";
constexpr const char* kFlinchLeftClipName = "flinch_left";
constexpr const char* kFlinchRightClipName = "flinch_right";
constexpr const char* kHitReactMediumClipName = "hit_react_medium";
constexpr const char* kHitReactHeavyClipName = "hit_react_heavy";
constexpr const char* kDeathClipName = "death";
constexpr const char* kKnockdownClipName = "stunned";

// Spawn an enemy actor into the shared pool. Caller must have
// already initialized the pool (player at index 0).
void spawnEnemyActor(float x, float z, float yaw)
{
    Actor e;
    e.controller = Controller::AI_Stationary;
    e.faction = Faction::Hostile;
    e.pos = glm::vec3(x, 0.0f, z);
    e.yaw = yaw;
    e.spawn_pos = e.pos;
    e.spawn_yaw = yaw;
    e.sampler = selva::anim::createPoseSampler(selva::anim::skeleton(), selva::anim::playerMesh());
    initActorPools(e.hp, e.stamina, e.poise, e.body, e.stats);
    if (const auto* idle = selva::anim::clips().get(kEnemyIdleClipName);
        idle != nullptr && idle->isLoaded())
        e.sampler.update(*idle, 0.0f, 0.0f);
    // Phase-stagger the first AI tick so a wave of actors spawned on
    // the same frame doesn't all evaluate together. The pool index is
    // the soon-to-be position of this actor (after push_back).
    const int pool_index = static_cast<int>(actors().size());
    seedAiTickPhase(e, pool_index, selva::tuning::current());
    actors().push_back(std::move(e));
}

// Pick the directional flinch clip from the hit normal rotated into
// the target's local frame. Convention: target yaw=0 faces -Z.
// Local forward = -Z; local right = +X.
//
// world_normal points attacker->target (impact impulse direction).
// The side STRUCK is the side facing the attacker, i.e. -world_normal.
// We rotate -world_normal into target-local space and pick the clip
// from the dominant axis.
const char* pickDirectionalFlinchClip(float target_yaw, const glm::vec3& world_normal)
{
    const glm::vec3 strike_dir = -world_normal; // target -> attacker
    const float cy = std::cos(-target_yaw);
    const float sy = std::sin(-target_yaw);
    const float local_x = cy * strike_dir.x + sy * strike_dir.z;
    const float local_z = -sy * strike_dir.x + cy * strike_dir.z;
    if (std::abs(local_x) > std::abs(local_z))
        return (local_x > 0.0f) ? kFlinchRightClipName : kFlinchLeftClipName;
    return (local_z < 0.0f) ? kFlinchFrontClipName : kFlinchBackClipName;
}

// Per-actor knockdown lifecycle. Plays the knockdown clip with
// freeze_last, then after `enemy_recovery_after_knockdown_seconds`
// releases the one-shot — sampler blends back to combat idle in
// place. Mirrors tickDeathLifecycle. Returns true while the actor
// is still mid-knockdown.
bool tickKnockdownLifecycle(Actor& a, float dt, const selva::anim::AnimationClip* knockdown_clip)
{
    if (!a.is_knocked_down)
        return false;
    const auto& tun = selva::tuning::current();
    const float now = selva::wallClock();
    if (a.knockdown_start_time > 0.0f &&
        (now - a.knockdown_start_time) >= tun.enemy_recovery_after_knockdown_seconds)
    {
        a.is_knocked_down = false;
        a.knockdown_start_time = -1.0f;
        a.sampler.releaseOneShot();
        selva::combat::combatLog("[knockdown] actor recovered\n");
        return false;
    }
    if (knockdown_clip != nullptr && knockdown_clip->isLoaded())
        a.sampler.update(*knockdown_clip, dt, 0.0f, /*loops=*/false);
    return true;
}

// Per-actor poise refill. Linear from 0 -> max over decay_window
// seconds, but only after decay_window has elapsed since the last
// poise-damage event. (Hit recently? wait. Settled? refill.)
void tickPoiseRefill(Actor& a, float dt)
{
    if (a.poise.current >= a.poise.max)
        return;
    const auto& tun = selva::tuning::current();
    const float now = selva::wallClock();
    if (a.poise.last_damage_time > 0.0f &&
        (now - a.poise.last_damage_time) < tun.poise_decay_window_seconds)
        return;
    // Refill linearly. Use the decay window as the time-to-max
    // duration so the refill rate is comprehensible: full bar
    // refills in N seconds after the cooldown ends.
    const float refill_rate = static_cast<float>(a.poise.max) / tun.poise_decay_window_seconds;
    const float gained = refill_rate * dt;
    a.poise.current = std::min(a.poise.max, a.poise.current + static_cast<int>(std::ceil(gained)));
}

// Per-actor death + respawn handling. Returns true if the actor
// should be skipped in this frame's idle update (dead and holding
// the death pose). Called once per AI actor per tick.
bool tickDeathLifecycle(Actor& a, float dt, const selva::anim::AnimationClip* death_clip)
{
    if (!a.is_dead)
        return false;
    const float respawn_delay = selva::tuning::current().enemy_respawn_after_death_seconds;
    if (a.death_time > 0.0f && (selva::wallClock() - a.death_time) >= respawn_delay)
    {
        selva::combat::combatLog("[enemy-respawn] respawning actor at t=%.3f\n",
                                 selva::wallClock());
        a.is_dead = false;
        a.death_time = -1.0f;
        a.last_damage_time = -1.0f;
        a.last_hit_react_time = -1.0f;
        a.pos = a.spawn_pos;
        a.yaw = a.spawn_yaw;
        initActorPools(a.hp, a.stamina, a.poise, a.body, a.stats);
        a.sampler.releaseOneShot();
        return false;
    }
    if (death_clip != nullptr && death_clip->isLoaded())
        a.sampler.update(*death_clip, dt, 0.0f, /*loops=*/false);
    return true;
}

// Translate the legacy enemy-view `index` (filtered list) to the
// shared actor pool. Returns nullptr if no enemy at that slot.
Actor* resolveEnemyByIndex(int index)
{
    auto& pool = actors();
    int seen = 0;
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;
        if (seen == index)
            return &a;
        ++seen;
    }
    return nullptr;
}

// Fire the death one-shot on `e` and mark dead. Caller already
// confirmed hp <= 0. Logs the event.
void fireEnemyDeath(Actor& e, int index)
{
    const auto* death_clip = selva::anim::clips().get(kDeathClipName);
    if (death_clip != nullptr && death_clip->isLoaded())
    {
        selva::anim::PoseSampler::OneShotOptions opts;
        opts.clip_key = kDeathClipName;
        opts.freeze_last = true;
        e.sampler.playOneShot(*death_clip, /*blend_in_seconds=*/0.25f,
                              /*blend_out_seconds=*/0.25f, selva::anim::PoseSampler::BodyMask::Full,
                              /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    }
    e.is_dead = true;
    e.death_time = selva::wallClock();
    selva::combat::combatLog("[enemy-death] enemy[%d] died\n", index);
}

// Fire the knockdown one-shot on `e` and set state. Caller already
// confirmed poise broke (current == 0).
void fireEnemyKnockdown(Actor& e, int index, int damage, int poise_damage, float now)
{
    const auto& tun = selva::tuning::current();
    const auto* knockdown_clip = selva::anim::clips().get(kKnockdownClipName);
    if (knockdown_clip != nullptr && knockdown_clip->isLoaded())
    {
        selva::anim::PoseSampler::OneShotOptions opts;
        opts.clip_key = kKnockdownClipName;
        opts.freeze_last = true;
        const float start_t = std::max(0.0f, tun.knockdown_clip_start_seconds);
        e.sampler.playOneShot(*knockdown_clip, /*blend_in_seconds=*/0.25f,
                              /*blend_out_seconds=*/0.25f, selva::anim::PoseSampler::BodyMask::Full,
                              start_t,
                              /*playback_rate=*/1.0f, opts);
        selva::combat::combatLog(
            "[knockdown] firing knockdown clip dur=%.3fs start=%.2fs end_tunable=%.2fs\n",
            knockdown_clip->duration(), start_t, tun.knockdown_clip_end_seconds);
    }
    e.is_knocked_down = true;
    e.knockdown_start_time = now;
    e.poise.current = e.poise.max; // reset on break
    e.last_damage_time = now;
    selva::combat::combatLog(
        "[knockdown] enemy[%d] poise broke (dmg=%d poise_dmg=%d) -> knockdown clip\n", index,
        damage, poise_damage);
}

// Pick the hit-react clip + blend timings from damage tier. For
// sub-medium damage falls through to a directional flinch.
struct HitReactPick
{
    const char* clip_name;
    float blend_in;
    float blend_out;
};
HitReactPick pickHitReactClip(int damage, float target_yaw, const glm::vec3& world_normal)
{
    const auto& tun = selva::tuning::current();
    if (static_cast<float>(damage) >= tun.hit_react_heavy_threshold)
        return {kHitReactHeavyClipName, 0.08f, 0.20f};
    if (static_cast<float>(damage) >= tun.hit_react_medium_threshold)
        return {kHitReactMediumClipName, 0.06f, 0.15f};
    return {pickDirectionalFlinchClip(target_yaw, world_normal), 0.04f, 0.15f};
}

} // namespace

void initHubEnemies()
{
    // Pool must already have the player at index 0; we append.
    // First enemy: stationary humanoid 6m north of the clearing.
    spawnEnemyActor(0.0f, -6.0f, 0.0f);
}

void shutdownHubEnemies()
{
    // Remove every AI actor from the pool; leave the player intact.
    auto& pool = actors();
    pool.erase(std::remove_if(pool.begin() + (pool.empty() ? 0 : 1), pool.end(),
                              [](const Actor& a) { return a.controller != Controller::Input; }),
               pool.end());
}

void tickEnemies(float dt)
{
    const auto* idle = selva::anim::clips().get(kEnemyIdleClipName);
    const auto* death = selva::anim::clips().get(kDeathClipName);
    const auto* knockdown = selva::anim::clips().get(kKnockdownClipName);
    const auto& tun = selva::tuning::current();
    const Actor& pc = player();
    for (auto& a : actors())
    {
        if (a.controller == Controller::Input)
            continue;
        // Perception runs even on dead/knocked-down actors so that
        // when they recover, awareness reflects the moment of recovery
        // (not a stale snapshot from when they fell). Cheap; no side
        // effects beyond writing to actor.perception.
        tickPerception(a, pc, dt, tun);
        if (tickDeathLifecycle(a, dt, death))
        {
            applyActorClipHipDelta(a);
            continue;
        }
        if (tickKnockdownLifecycle(a, dt, knockdown))
        {
            applyActorClipHipDelta(a);
            continue;
        }
        // Decision tick gate. Throttled to ai_decision_tick_hz
        // (default 10Hz). Sprint 4 will replace the diagnostic log
        // with the behavior tree evaluation.
        if (shouldTickAi(a, tun))
        {
            if (tun.debug_ai_tick_log)
                selva::combat::combatLog(
                    "[ai-tick] actor pool_idx=%td awareness=%d t=%.3f\n", &a - &actors().front(),
                    static_cast<int>(a.perception.awareness), selva::wallClock());
        }
        tickPoiseRefill(a, dt);
        if (idle != nullptr && idle->isLoaded())
            a.sampler.update(*idle, dt, 0.0f);
        applyActorClipHipDelta(a);
    }
}

std::vector<Actor*> enemies()
{
    std::vector<Actor*> out;
    for (auto& a : actors())
    {
        if (a.controller != Controller::Input)
            out.push_back(&a);
    }
    return out;
}

void playEnemyHitReact(int index, int damage, int poise_damage, const glm::vec3& world_normal)
{
    Actor* target = resolveEnemyByIndex(index);
    if (target == nullptr)
        return;
    Actor& e = *target;
    // Hit immunity: dead / mid-knockdown actors don't react.
    if (e.is_dead || e.is_knocked_down)
        return;

    // Death takes priority over any other reaction.
    if (e.hp.current <= 0)
    {
        fireEnemyDeath(e, index);
        return;
    }

    const auto& tun = selva::tuning::current();
    const float now = selva::wallClock();

    // Apply poise damage. If poise breaks, fire knockdown chain
    // (overrides the normal hit-react tier).
    e.poise.current = std::max(0, e.poise.current - poise_damage);
    e.poise.last_damage_time = now;
    if (e.poise.current == 0)
    {
        fireEnemyKnockdown(e, index, damage, poise_damage, now);
        return;
    }

    if (e.last_hit_react_time > 0.0f &&
        (now - e.last_hit_react_time) < tun.hit_react_cooldown_seconds)
        return;

    const HitReactPick pick = pickHitReactClip(damage, e.yaw, world_normal);
    const auto* clip = selva::anim::clips().get(pick.clip_name);
    if (clip == nullptr || !clip->isLoaded())
        return;
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = pick.clip_name;
    e.sampler.playOneShot(*clip, pick.blend_in, pick.blend_out,
                          selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    e.last_hit_react_time = now;
    e.last_damage_time = now;
    selva::combat::combatLog("[hit-react] enemy[%d] dmg=%d poise=%d/%d -> clip=%s\n", index, damage,
                             e.poise.current, e.poise.max, pick.clip_name);
}

} // namespace selva::gameplay
