#include "gameplay/Enemies.h"

#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "combat/CombatLog.h"
#include "gameplay/AiTick.h"
#include "gameplay/EnemyArchetype.h"
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
// Idle clip pair. Peaceful idle plays when the enemy is unaware or
// merely suspicious of the player — visually communicates "this
// thing isn't engaged with me." Combat idle (the bouncy ready-stance)
// takes over once awareness escalates to Alerted/Combat. Flinch /
// hit-react clips were authored against combat idle, so an actor in
// peaceful idle will briefly seam at the hip when struck; that's
// acceptable because being struck implies an aggressor is present
// and the enemy was about to escalate anyway.
constexpr const char* kEnemyPeacefulIdleClipName = "standard_idle";
constexpr const char* kEnemyCombatIdleClipName = "unarmed_combat_idle";
// Walking clip + speed threshold below which the actor plays an idle
// clip instead. Same Mixamo walk the player uses; per-archetype
// overrides when the bestiary expands.
constexpr const char* kEnemyWalkClipName = "walking";
constexpr float kEnemyWalkSpeedFloor = 0.15f; // m/s; above = walk, below = idle
constexpr const char* kFlinchFrontClipName = "flinch_front";
constexpr const char* kFlinchBackClipName = "flinch_back";
constexpr const char* kFlinchLeftClipName = "flinch_left";
constexpr const char* kFlinchRightClipName = "flinch_right";
constexpr const char* kHitReactMediumClipName = "hit_react_medium";
constexpr const char* kHitReactHeavyClipName = "hit_react_heavy";
constexpr const char* kDeathClipName = "death";
constexpr const char* kKnockdownClipName = "stunned";

// Spawn an enemy actor into the shared pool. Caller must have
// already initialized the pool (player at index 0). `archetype_id`
// is looked up in the archetype registry; nullptr/missing = no
// archetype bound (test-dummy fallback behavior).
void spawnEnemyActor(float x, float z, float yaw, const char* archetype_id)
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
    // Prime the sampler with peaceful idle — spawn awareness is
    // always Unaware. The pose-snapshot path in the sampler will
    // handle the eventual combat-idle swap when awareness escalates.
    if (const auto* idle = selva::anim::clips().get(kEnemyPeacefulIdleClipName);
        idle != nullptr && idle->isLoaded())
        e.sampler.update(*idle, 0.0f, 0.0f);
    if (archetype_id != nullptr && archetype_id[0] != '\0')
    {
        e.archetype = archetypes().get(archetype_id);
        if (e.archetype == nullptr)
            selva::combat::combatLog("[spawn] archetype '%s' not found in registry\n",
                                     archetype_id);
    }
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
// releases the one-shot — sampler blends back to whatever loco clip
// was bound underneath. Mirrors tickDeathLifecycle. Returns true
// while the actor is still mid-knockdown.
//
// `idle_clip` is passed as the loco-track target so the loco slot
// remains bound to a *real loco clip* throughout the knockdown.
// Passing the knockdown clip itself here would swap the loco track
// to a one-shot clip at blend=0 (visible as a hard pop), and on
// release the loco track would have to blend back to idle producing
// a second visible transition. Idle as the loco target = the one-
// shot occludes the loco visually, the loco track stays semantically
// correct, and recovery is seamless.
bool tickKnockdownLifecycle(Actor& a, float dt, const selva::anim::AnimationClip* idle_clip,
                            const char* idle_key)
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
    if (idle_clip != nullptr && idle_clip->isLoaded())
        a.sampler.update(*idle_clip, dt, /*blend_seconds=*/0.20f, /*loops=*/true, idle_key);
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

// Compute the world-space yaw for which `actor` faces `target_pos`.
// Convention: yaw=0 faces -Z. atan2(-dx, -dz) gives the yaw that
// rotates the forward vector (-Z) onto (target - actor) in XZ.
float yawFacing(const glm::vec3& actor_pos, const glm::vec3& target_pos)
{
    const float dx = target_pos.x - actor_pos.x;
    const float dz = target_pos.z - actor_pos.z;
    if ((dx * dx + dz * dz) < 1e-6f)
        return 0.0f;
    return std::atan2(-dx, -dz);
}

// Decision tick: read perception, write intent_xz + turn_intent_yaw.
// Called at the scheduled rate (ai_decision_tick_hz), so a 100ms-stale
// decision is the worst-case latency between a perception change and
// an intent change. Sprint 4a's behavior set is hardcoded; Sprint 4b
// replaces this body with the behavior-tree evaluator.
//
//   Unaware    → no intent, hold spawn yaw
//   Suspicious → face last-known player position, no movement
//   Alerted    → face + approach last_known_player_pos until inside
//                combat_engage_range
//   Combat     → face + approach last_known_player_pos until inside
//                combat_engage_range (same as Alerted; the difference
//                is which non-locomotion actions are legal — Sprint 4b)
//
// Combat shares Alerted's locomotion intentionally: if the player
// steps out of melee, the enemy follows rather than standing still
// waiting for the disengage timer. Souls enemies do the same.
//
// All facing is driven by last_known_player_pos, not the live player
// position — when the player breaks line of sight the enemy keeps
// facing where it last saw them, which sells the perception layer.
void tickEnemyDecision(Actor& a, const selva::tuning::Tunables& tun)
{
    using selva::gameplay::Awareness;
    const Awareness aw = a.perception.awareness;
    if (aw == Awareness::Unaware)
    {
        a.intent_xz = glm::vec2(0.0f);
        a.turn_intent_yaw = a.spawn_yaw;
    }
    else
    {
        a.turn_intent_yaw = yawFacing(a.pos, a.perception.last_known_player_pos);
        // Suspicious stands and looks; Alerted + Combat approach to
        // engagement range.
        const bool should_approach = (aw == Awareness::Alerted) || (aw == Awareness::Combat);
        if (should_approach)
        {
            const float dx = a.perception.last_known_player_pos.x - a.pos.x;
            const float dz = a.perception.last_known_player_pos.z - a.pos.z;
            const float dist_sq = dx * dx + dz * dz;
            const float stop_sq =
                tun.ai_combat_engage_range_meters * tun.ai_combat_engage_range_meters;
            if (dist_sq > stop_sq && dist_sq > 1e-6f)
            {
                const float dist = std::sqrt(dist_sq);
                a.intent_xz = glm::vec2(dx / dist, dz / dist) * tun.walk_speed;
            }
            else
            {
                a.intent_xz = glm::vec2(0.0f);
            }
        }
        else
        {
            a.intent_xz = glm::vec2(0.0f);
        }
    }
    if (tun.debug_ai_decision_log)
    {
        const auto& p = a.perception.last_known_player_pos;
        selva::combat::combatLog(
            "[ai-decision] awareness=%d target=(%.2f,%.2f) intent=(%.2f,%.2f) yaw=%.2f\n",
            static_cast<int>(aw), p.x, p.z, a.intent_xz.x, a.intent_xz.y, a.turn_intent_yaw);
    }
}

// Per-frame locomotion: advance velocity toward intent_xz, advance
// yaw toward turn_intent_yaw, integrate position. Called every render
// frame on every AI actor (not gated by the AI scheduler — animation
// and motion must run at full rate). Mirrors the player's velocity
// ramp + turn-rate convention so PC/NPC locomotion behaves the same.
void tickEnemyLocomotion(Actor& a, float dt, const selva::tuning::Tunables& tun)
{
    // Velocity ramp toward intent.
    const glm::vec2 delta = a.intent_xz - a.velocity_xz;
    const float delta_mag = glm::length(delta);
    if (delta_mag > 0.0001f)
    {
        const float current_speed = glm::length(a.velocity_xz);
        const float target_speed = glm::length(a.intent_xz);
        const float rate =
            (target_speed >= current_speed) ? tun.locomotion_accel : tun.locomotion_decel;
        const float max_step = rate * dt;
        if (delta_mag <= max_step)
            a.velocity_xz = a.intent_xz;
        else
            a.velocity_xz += (delta / delta_mag) * max_step;
    }

    // Integrate XZ position from velocity.
    a.pos.x += a.velocity_xz.x * dt;
    a.pos.z += a.velocity_xz.y * dt;

    // Turn yaw toward turn_intent_yaw, shortest-path. Wrap delta into
    // [-pi, pi] so a 350° desired yaw doesn't take the long way around.
    constexpr float kTwoPi = 6.2831853f;
    constexpr float kPi = 3.1415927f;
    float yaw_delta = a.turn_intent_yaw - a.yaw;
    while (yaw_delta > kPi)
        yaw_delta -= kTwoPi;
    while (yaw_delta < -kPi)
        yaw_delta += kTwoPi;
    const float max_yaw_step = tun.ai_turn_rate_radians_per_sec * dt;
    if (std::abs(yaw_delta) <= max_yaw_step)
        a.yaw = a.turn_intent_yaw;
    else
        a.yaw += (yaw_delta > 0.0f ? max_yaw_step : -max_yaw_step);
}

// Per-actor death + respawn handling. Returns true if the actor
// should be skipped in this frame's idle update (dead and holding
// the death pose). Called once per AI actor per tick.
//
// `idle_clip` is passed as the loco-track target so the loco slot
// remains bound to a real loco clip throughout the death + respawn
// cycle (same rationale as tickKnockdownLifecycle).
bool tickDeathLifecycle(Actor& a, float dt, const selva::anim::AnimationClip* idle_clip,
                        const char* idle_key)
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
    if (idle_clip != nullptr && idle_clip->isLoaded())
        a.sampler.update(*idle_clip, dt, /*blend_seconds=*/0.20f, /*loops=*/true, idle_key);
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
    // The dummy is bound to the limbo_shade archetype so Sprint 4's
    // behavior tree will pick from its declared actions.
    spawnEnemyActor(0.0f, -6.0f, 0.0f, "limbo_shade");
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
    const auto* peaceful_idle = selva::anim::clips().get(kEnemyPeacefulIdleClipName);
    const auto* combat_idle = selva::anim::clips().get(kEnemyCombatIdleClipName);
    const auto* walk = selva::anim::clips().get(kEnemyWalkClipName);
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
        // Death + knockdown lifecycles pass the actor's idle clip as
        // the loco-track target so the loco slot stays bound to a
        // real loco clip during the freeze_last one-shot. Awareness
        // determines whether peaceful or combat idle is the right
        // underlying loco — matches the live picker below.
        const bool engaged_now = a.perception.awareness >= Awareness::Alerted;
        const auto* lifecycle_idle = engaged_now ? combat_idle : peaceful_idle;
        const char* lifecycle_idle_key =
            engaged_now ? kEnemyCombatIdleClipName : kEnemyPeacefulIdleClipName;
        if (tickDeathLifecycle(a, dt, lifecycle_idle, lifecycle_idle_key))
        {
            applyActorClipHipDelta(a);
            continue;
        }
        if (tickKnockdownLifecycle(a, dt, lifecycle_idle, lifecycle_idle_key))
        {
            applyActorClipHipDelta(a);
            continue;
        }
        // Decision tick gate. Throttled to ai_decision_tick_hz
        // (default 10Hz). Sprint 4a: hardcoded perception → intent
        // mapping. Sprint 4b: behavior tree evaluation replaces the
        // body of tickEnemyDecision.
        if (shouldTickAi(a, tun))
        {
            if (tun.debug_ai_tick_log)
                selva::combat::combatLog(
                    "[ai-tick] actor pool_idx=%td awareness=%d t=%.3f\n", &a - &actors().front(),
                    static_cast<int>(a.perception.awareness), selva::wallClock());
            tickEnemyDecision(a, tun);
        }
        // Locomotion runs at full render rate (not gated) so motion
        // stays smooth; the decision tick only updates the *intent*
        // every ~100ms, locomotion integrates it every frame.
        tickEnemyLocomotion(a, dt, tun);
        tickPoiseRefill(a, dt);
        // Pick the locomotion clip. Three states, in priority order:
        //   walking   — speed above floor, any awareness
        //   combat-idle — speed below floor, awareness >= Alerted
        //   peaceful-idle — speed below floor, awareness < Alerted
        // The combat-stance switch happens on the first sighting that
        // escalates to Alerted (or higher). Visually communicates
        // "the enemy registered you" without an explicit telegraph.
        // Single threshold, no hysteresis/cooldown — the sampler
        // handles rapid mid-blend swaps via the pose-snapshot path
        // in applyLocoCrossfade. Walking clip has authored hip
        // motion which applyActorClipHipDelta consumes — feet stay
        // planted while gameplay-driven velocity moves the actor.
        const float speed = glm::length(a.velocity_xz);
        const bool is_walking =
            (speed > kEnemyWalkSpeedFloor) && walk != nullptr && walk->isLoaded();
        const bool engaged = a.perception.awareness >= Awareness::Alerted;
        const selva::anim::AnimationClip* idle_clip = engaged ? combat_idle : peaceful_idle;
        const char* idle_key = engaged ? kEnemyCombatIdleClipName : kEnemyPeacefulIdleClipName;
        const selva::anim::AnimationClip* loco_clip = is_walking ? walk : idle_clip;
        const char* loco_key = is_walking ? kEnemyWalkClipName : idle_key;
        if (loco_clip != nullptr && loco_clip->isLoaded())
            a.sampler.update(*loco_clip, dt, /*blend_seconds=*/0.20f, /*loops=*/true, loco_key);
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
