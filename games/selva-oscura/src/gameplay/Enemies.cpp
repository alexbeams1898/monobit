#include "gameplay/Enemies.h"

#include "Formulas.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletonJointMap.h"
#include "audio/Audio.h"
#include "combat/CombatLog.h"
#include "gameplay/AiBarriers.h"
#include "gameplay/AiTick.h"
#include "gameplay/BehaviorTree.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/Perception.h"
#include "world/Collision.h"
#include "world/Terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
constexpr const char* kEnemyWalkBackClipName = "walking_backward";
constexpr const char* kEnemyStrafeLeftClipName = "strafe_walking_left";
constexpr const char* kEnemyStrafeRightClipName = "strafe_walking_right";
constexpr float kEnemyWalkSpeedFloor = 0.15f; // m/s; above = walk, below = idle
constexpr const char* kFlinchFrontClipName = "flinch_front";
constexpr const char* kFlinchBackClipName = "flinch_back";
constexpr const char* kFlinchLeftClipName = "flinch_left";
constexpr const char* kFlinchRightClipName = "flinch_right";
constexpr const char* kHitReactMediumClipName = "hit_react_medium";
constexpr const char* kHitReactHeavyClipName = "hit_react_heavy";
constexpr const char* kDeathClipName = "death";
constexpr const char* kKnockdownClipName = "stunned";

// Spawn an enemy actor from a parsed declaration. Caller must have
// already initialized the pool (player at index 0). The decl's
// archetype_id is looked up in the archetype registry; empty/missing
// = no archetype bound (test-dummy fallback behavior).
//
// Y is left at decl.pos.y at spawn time; per-frame tickEnemyLocomotion
// snaps it to ground height via groundHeight() if outdoor terrain
// owns the spot. Authors get to declare the explicit Y for indoor /
// suspended-platform geometry.
void spawnEnemyFromDecl(const std::string& region_id, const EnemySpawnDecl& decl)
{
    Actor e;
    e.controller = Controller::AI_Stationary;
    e.faction = Faction::Hostile;
    // Resolve pos -- pos.y may be the "auto_terrain" sentinel,
    // in which case sample the terrain at XZ for the spawn Y.
    e.pos = decl.pos;
    if (decl.pos_y_auto_terrain)
        e.pos.y = selva::world::groundHeight(decl.pos.x, decl.pos.z,
                                             -std::numeric_limits<float>::infinity());
    e.yaw = decl.yaw;
    e.spawn_pos = e.pos;
    e.spawn_yaw = decl.yaw;
    e.spawn_id = region_id + ":" + decl.id;
    e.spawn_region_id = region_id;
    e.permanent_on_death = decl.permanent_on_death;
    // Bind sampler to the archetype's skeleton + mesh. Humanoid
    // archetypes share the player rig (skeleton_id="player"); wolf
    // and other non-humanoids bind to their own skeleton from the
    // registry. Joint map is looked up by the same key. See
    // [[design/animals_and_multi_skeleton]] for the architecture.
    const std::string sk_id =
        (e.archetype != nullptr) ? e.archetype->skeleton_id : std::string("player");
    e.sampler = selva::anim::createPoseSampler(selva::anim::skeletonByKey(sk_id),
                                               selva::anim::meshByKey(sk_id),
                                               selva::anim::jointMapByKey(sk_id));
    initActorPools(e.hp, e.stamina, e.poise, e.body, e.stats);
    // Seed per-actor RNG. Two actors of the same archetype get
    // independent rolls so they don't synchronize their weighted
    // action picks. random_device + a salt from spawn pos makes
    // co-spawned actors diverge on the first call.
    std::random_device rd;
    e.rng.seed(rd() ^ static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.x * 1000.0f)) ^
               static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.z * 1000.0f)));
    // Prime the sampler with peaceful idle — spawn awareness is
    // always Unaware. The pose-snapshot path in the sampler will
    // handle the eventual combat-idle swap when awareness escalates.
    if (const auto* idle = selva::anim::clips().get(kEnemyPeacefulIdleClipName);
        idle != nullptr && idle->isLoaded())
        e.sampler.update(*idle, 0.0f, 0.0f);
    if (!decl.archetype.empty())
    {
        e.archetype = archetypes().get(decl.archetype);
        if (e.archetype == nullptr)
            selva::combat::combatLog("[spawn] archetype '{}' not found in registry (id='{}')",
                                     decl.archetype, e.spawn_id);
    }
    // Skeleton: archetype's id (default "player"). Resolved by
    // selva::anim::meshByKey / skeletonByKey at use sites.
    e.skeleton_id = (e.archetype != nullptr) ? e.archetype->skeleton_id : std::string("player");
    // Hurtbox decls. Archetype's own list wins; if archetype declared
    // none, the actor inherits the player's hurtbox layout (every
    // humanoid shade today -- they share the X_Bot skeleton). Wolves
    // and other non-humanoid archetypes set their own hurtboxes in
    // their archetype JSON.
    if (e.archetype != nullptr && !e.archetype->hurtbox_decls.empty())
        e.body.hurtbox_decls = e.archetype->hurtbox_decls;
    else
        e.body.hurtbox_decls = selva::gameplay::player().body.hurtbox_decls;
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
        selva::combat::combatLog("[knockdown] actor recovered");
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
    const auto& f = selva::formulas::current();
    const float now = selva::wallClock();
    if (a.poise.last_damage_time > 0.0f && (now - a.poise.last_damage_time) < f.poise.decay_window)
        return;
    // Refill linearly: full bar refills over decay_window seconds once the
    // cooldown elapses. Float math -- see Stamina::current rationale.
    const float refill_rate = a.poise.max / f.poise.decay_window;
    a.poise.current = std::min(a.poise.max, a.poise.current + refill_rate * dt);
}

// Decision tick: traverse the actor's bound behavior tree. The tree
// reads perception + writes intent_xz / turn_intent_yaw (and, Sprint
// 4b commit 2 onward, fires action one-shots + sets cooldowns).
// Called at the scheduled rate (ai_decision_tick_hz), so a 100ms-
// stale decision is the worst-case latency between a perception
// change and an intent change.
//
// Tree binding: actor.archetype->tree_id selects which tree runs.
// "humanoid_basic" is the default and covers every humanoid in the
// bestiary until a specific archetype demands its own builder. If
// the actor has no archetype bound (test-dummy fallback path) or
// the tree id is unknown, this is a no-op.
void tickEnemyDecision(Actor& a, const selva::tuning::Tunables& tun)
{
    const BehaviorTree* tree = nullptr;
    if (a.archetype != nullptr)
        tree = behaviorTrees().get(a.archetype->tree_id);
    else
        tree = behaviorTrees().get("humanoid_basic");
    if (tree != nullptr)
        tree->tick(a, tun);

    if (tun.debug_ai_decision_log)
    {
        const auto& p = a.perception.last_known_player_pos;
        selva::combat::combatLog(
            "[ai-decision] awareness={} target=({:.2f},{:.2f}) intent=({:.2f},{:.2f}) "
            "yaw={:.2f} tree={}",
            static_cast<int>(a.perception.awareness), p.x, p.z, a.intent_xz.x, a.intent_xz.y,
            a.turn_intent_yaw,
            (a.archetype != nullptr) ? a.archetype->tree_id.c_str() : "humanoid_basic");
    }
}

// Per-frame locomotion: advance velocity toward intent_xz, advance
// yaw toward turn_intent_yaw, integrate position. Called every render
// frame on every AI actor (not gated by the AI scheduler — animation
// and motion must run at full rate). Mirrors the player's velocity
// ramp + turn-rate convention so PC/NPC locomotion behaves the same.
void tickEnemyLocomotion(Actor& a, float dt, const selva::tuning::Tunables& tun)
{
    // One-shot lock — mirrors the PC's movement_locked / velocity_locked
    // gates in tickPlayerVelocity. While a one-shot is in flight, the
    // clip-hip path (applyActorClipHipDelta at the end of tickEnemies)
    // owns translation; this function must not also integrate velocity*dt
    // into pos, or the two translation sources compound and the actor
    // slides during the swing. Zeroing velocity here also ensures that
    // when the one-shot ends the actor starts from a clean state — the
    // next intent (from the next decision tick) ramps velocity up from
    // zero rather than carrying stale pre-swing momentum into the
    // recovery.
    if (a.sampler.isOneShotActive())
    {
        a.velocity_xz = glm::vec2(0.0f);
    }
    else
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

        // Integrate XZ position from velocity. Only when not in a
        // one-shot — clip-hip drives translation during the swing.
        //
        // AI barrier check: per [[gameplay/AiBarriers.h]], an actor
        // cannot enter ai_block_volumes belonging to a region other
        // than its own. A Limbo shade is blocked from walking into
        // the chapel/descent corridor (those volumes are owned by
        // chapel_interior / chapel_exterior, not by limbo). Apply
        // the move axis-by-axis so a barrier on the X face still
        // lets the actor slide along Z (and vice versa) -- standard
        // axis-projection AABB-slide pattern.
        const float new_x = a.pos.x + a.velocity_xz.x * dt;
        const float new_z = a.pos.z + a.velocity_xz.y * dt;
        const glm::vec3 try_x(new_x, a.pos.y, a.pos.z);
        if (findAiBlockingVolume(try_x, a.spawn_region_id) == nullptr)
            a.pos.x = new_x;
        else
            a.velocity_xz.x = 0.0f;
        const glm::vec3 try_z(a.pos.x, a.pos.y, new_z);
        if (findAiBlockingVolume(try_z, a.spawn_region_id) == nullptr)
            a.pos.z = new_z;
        else
            a.velocity_xz.y = 0.0f;
    }
    // Snap Y to the ground so the actor's feet stay on the heightmap
    // surface (or on any walkable BoxCollider like a stair step).
    a.pos.y = selva::world::groundHeight(a.pos.x, a.pos.z, a.pos.y);

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
    // Cycle-respawn model: dead enemies stay dead until the next
    // cycle boundary (player second-death or load-game), at which
    // point resetCycleEnemies fires. While dead, the death-clip
    // freeze_last holds the pose; we keep ticking the sampler with
    // an idle update to keep its state machine alive even though
    // the visible pose is frozen. See docs/design/setting.md
    // "Cycle structure" + "Per-circle reactivity".
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
            "[knockdown] firing knockdown clip dur={:.3f}s start={:.2f}s end_tunable={:.2f}s",
            knockdown_clip->duration(), start_t, tun.knockdown_clip_end_seconds);
    }
    e.is_knocked_down = true;
    e.knockdown_start_time = now;
    e.poise.current = e.poise.max; // reset on break
    e.last_damage_time = now;
    selva::combat::combatLog(
        "[knockdown] enemy[{}] poise broke (dmg={} poise_dmg={}) -> knockdown clip", index, damage,
        poise_damage);
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

// Public — exposed so the PC's death path in PerFrameTick can fire
// the same one-shot routing as enemies do. Reads `e.death_clip_name`
// so each actor controls its own visual; the PC's is "second_death".
void fireEnemyDeath(Actor& e, int index)
{
    const char* clip_name = e.death_clip_name.c_str();
    const auto* death_clip = selva::anim::clips().get(e.death_clip_name);
    if (death_clip != nullptr && death_clip->isLoaded())
    {
        selva::anim::PoseSampler::OneShotOptions opts;
        opts.clip_key = clip_name;
        opts.freeze_last = true;
        e.sampler.playOneShot(*death_clip, /*blend_in_seconds=*/0.25f,
                              /*blend_out_seconds=*/0.25f, selva::anim::PoseSampler::BodyMask::Full,
                              /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    }
    // Bed layer — plays at clip start (the dread under the fall).
    if (!e.death_sfx_name.empty())
    {
        selva::combat::combatLog("[death-audio] bed sfx='{}'", e.death_sfx_name);
        selva::audio::playSfx(e.death_sfx_name);
    }
    e.is_dead = true;
    e.death_time = selva::wallClock();
    // Peak-aligned layers — each scheduled so its declared peak
    // lands at death_time + peak_align_seconds. PC uses this to
    // stack synth_echo + soul_steal both peaking together at the
    // moment the second-death card snaps in.
    for (const auto& sfx_name : e.death_peak_sfx_names)
    {
        const float peak_offset = selva::audio::sfxPeakOffset(sfx_name);
        const float play_at = e.death_time + e.death_peak_align_seconds - peak_offset;
        selva::combat::combatLog(
            "[death-audio] scheduled sfx='{}' peak_off={:.3f}s align={:.3f}s play_at={:.3f}",
            sfx_name, peak_offset, e.death_peak_align_seconds, play_at);
        selva::audio::scheduleSfx(sfx_name, play_at);
    }
    // Player-only: duck the OST so the death audio reads clean
    // against a muffled background. Restored on respawn in
    // tickPlayerSecondDeathLifecycle.
    if (e.controller == Controller::Input)
        selva::audio::duckMusic();
    selva::combat::combatLog("[death] actor[{}] died (clip={})", index, clip_name);
}

void spawnRegionEnemies(const std::string& region_id, const std::vector<EnemySpawnDecl>& decls)
{
    for (const auto& d : decls)
        spawnEnemyFromDecl(region_id, d);
    selva::combat::combatLog("[spawn-region] region='{}' spawned {} enemy actor(s)", region_id,
                             decls.size());
}

void shutdownHubEnemies()
{
    // Remove every AI actor from the pool; leave the player intact.
    auto& pool = actors();
    pool.erase(std::remove_if(pool.begin() + (pool.empty() ? 0 : 1), pool.end(),
                              [](const Actor& a) { return a.controller != Controller::Input; }),
               pool.end());
}

void resetCycleEnemies()
{
    auto& pool = actors();
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;

        // Permanent-on-death keepers that fell stay fallen across
        // cycles. The Souls "felled keeper does not respawn" canon
        // per docs/design/fallback.md "Class persists across cycles
        // / Keepers felled".
        if (a.permanent_on_death && a.is_dead)
            continue;

        a.pos = a.spawn_pos;
        a.yaw = a.spawn_yaw;
        a.velocity_xz = glm::vec2(0.0f);
        a.intent_xz = glm::vec2(0.0f);
        a.turn_intent_yaw = a.spawn_yaw;

        // Mortal pools back to max from archetype Body + Stats.
        initActorPools(a.hp, a.stamina, a.poise, a.body, a.stats);

        a.is_dead = false;
        a.death_time = -1.0f;
        a.is_knocked_down = false;
        a.knockdown_start_time = -1.0f;
        a.last_damage_time = -1.0f;
        a.last_hit_react_time = -1.0f;

        // Perception cleared - first-sighting double-take must replay.
        a.perception = PerceptionState{};

        a.lock_target_idx = -1;
        a.duel_strafe_dir = 0;

        a.action_state.clear();
        a.active_attack_hitbox_id = 0;
        a.active_attack_joint_idx = -1;
        a.active_attack_tip_offset_z = 0.0f;

        a.foot_left = Actor::FootContact{};
        a.foot_right = Actor::FootContact{};

        a.sampler.releaseOneShot();
        if (const auto* idle = selva::anim::clips().get(kEnemyPeacefulIdleClipName);
            idle != nullptr && idle->isLoaded())
            a.sampler.update(*idle, 0.0f, 0.0f);
    }
}

// Update the actor's lock_target_idx based on awareness. Idempotent
// per tick; logs the edges (acquire/release). On acquisition the
// strafe direction is rolled once and held for the engagement.
static void updateEnemyLockOnPlayer(Actor& a, const Actor& pc)
{
    const bool should_lock =
        a.perception.awareness == Awareness::Combat && !a.is_dead && !pc.is_dead;
    if (should_lock && a.lock_target_idx != 0)
    {
        a.lock_target_idx = 0;
        a.duel_strafe_dir = (std::uniform_int_distribution<int>(0, 1)(a.rng) == 0) ? -1 : 1;
        selva::combat::combatLog("[ai-lock] enemy acquired target (combat entry, strafe={})",
                                 a.duel_strafe_dir > 0 ? "right" : "left");
    }
    else if (!should_lock && a.lock_target_idx >= 0)
    {
        a.lock_target_idx = -1;
        a.duel_strafe_dir = 0;
        selva::combat::combatLog("[ai-lock] enemy released target");
    }
}

// Pick this frame's locomotion clip for an enemy. Three states by
// priority: walking > combat-idle > peaceful-idle. When locked + moving,
// directional override picks a strafe/backward variant from the
// shared PC/NPC picker.
struct EnemyLocoPick
{
    const selva::anim::AnimationClip* clip;
    const char* key;
};
static EnemyLocoPick pickEnemyLocomotionClip(const Actor& a, const selva::anim::AnimationClip* walk,
                                             const selva::anim::AnimationClip* combat_idle,
                                             const selva::anim::AnimationClip* peaceful_idle)
{
    const float speed = glm::length(a.velocity_xz);
    const bool is_walking = (speed > kEnemyWalkSpeedFloor) && walk != nullptr && walk->isLoaded();
    const bool engaged = a.perception.awareness >= Awareness::Alerted;
    EnemyLocoPick out;
    out.clip = is_walking ? walk : (engaged ? combat_idle : peaceful_idle);
    out.key = is_walking ? kEnemyWalkClipName
                         : (engaged ? kEnemyCombatIdleClipName : kEnemyPeacefulIdleClipName);
    if (!is_walking || a.lock_target_idx < 0)
        return out;
    // Directional override: shared with PC via directionalLocoClip.
    // Basis from turn_intent_yaw (target facing) not actor.yaw — yaw
    // lags by turn rate and would thrash the picker basis frame-to-
    // frame.
    const float yaw = a.turn_intent_yaw;
    const glm::vec3 fwd(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    const glm::vec3 intent3(a.intent_xz.x, 0.0f, a.intent_xz.y);
    const char* k = directionalLocoClip(fwd, right, intent3, /*running=*/false);
    if (k == nullptr)
        return out;
    const auto* c = selva::anim::clips().get(k);
    if (c != nullptr && c->isLoaded())
    {
        out.clip = c;
        out.key = k;
    }
    return out;
}

// Per-actor body of tickEnemies. Returns true if the lifecycle
// short-circuited (death/knockdown handled the actor this frame and
// the locomotion path should be skipped).
static bool tickOneEnemy(Actor& a, const Actor& pc, float dt, const selva::tuning::Tunables& tun,
                         const selva::anim::AnimationClip* walk,
                         const selva::anim::AnimationClip* combat_idle,
                         const selva::anim::AnimationClip* peaceful_idle)
{
    tickPerception(a, pc, dt, tun);
    updateEnemyLockOnPlayer(a, pc);
    const bool engaged_now = a.perception.awareness >= Awareness::Alerted;
    const auto* lifecycle_idle = engaged_now ? combat_idle : peaceful_idle;
    const char* lifecycle_idle_key =
        engaged_now ? kEnemyCombatIdleClipName : kEnemyPeacefulIdleClipName;
    if (tickDeathLifecycle(a, dt, lifecycle_idle, lifecycle_idle_key))
    {
        applyActorClipHipDelta(a);
        return true;
    }
    if (tickKnockdownLifecycle(a, dt, lifecycle_idle, lifecycle_idle_key))
    {
        applyActorClipHipDelta(a);
        return true;
    }
    if (shouldTickAi(a, tun))
    {
        if (tun.debug_ai_tick_log)
            selva::combat::combatLog("[ai-tick] actor pool_idx={} awareness={} t={:.3f}",
                                     &a - &actors().front(),
                                     static_cast<int>(a.perception.awareness), selva::wallClock());
        tickEnemyDecision(a, tun);
    }
    tickEnemyLocomotion(a, dt, tun);
    tickPoiseRefill(a, dt);
    const EnemyLocoPick pick = pickEnemyLocomotionClip(a, walk, combat_idle, peaceful_idle);
    if (pick.clip != nullptr && pick.clip->isLoaded())
        a.sampler.update(*pick.clip, dt, /*blend_seconds=*/0.20f, /*loops=*/true, pick.key);
    applyActorClipHipDelta(a);
    return false;
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
        tickOneEnemy(a, pc, dt, tun, walk, combat_idle, peaceful_idle);
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

int enemyIndex(const Actor& actor)
{
    int seen = 0;
    for (const auto& a : actors())
    {
        if (a.controller == Controller::Input)
            continue;
        if (&a == &actor)
            return seen;
        ++seen;
    }
    return -1;
}

void playEnemyHitReact(int index, int damage, int poise_damage, const glm::vec3& world_normal,
                       const glm::vec3& attacker_pos)
{
    Actor* target = resolveEnemyByIndex(index);
    if (target == nullptr)
        return;
    Actor& e = *target;
    // Hit immunity: dead / mid-knockdown actors don't react.
    if (e.is_dead || e.is_knocked_down)
        return;

    // Force-aggro on hit. Vision-based perception alone misses the
    // case where the player attacks from behind / out of cone. Souls
    // rule: getting hit always engages. Set awareness to Combat and
    // seed last_known_player_pos so the BT's LeafMoveToTarget and
    // LeafIdleFace face the right direction immediately. Combat
    // decay (ai_combat_disengage_seconds of no contact) handles
    // gradual de-aggro normally.
    e.perception.awareness = Awareness::Combat;
    e.perception.last_seen_time = selva::wallClock();
    e.perception.last_known_player_pos = attacker_pos;
    e.perception.suspicious_sighting_count = 0;

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
    e.poise.current = std::max(0.0f, e.poise.current - static_cast<float>(poise_damage));
    e.poise.last_damage_time = now;
    if (e.poise.current <= 0.0f)
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
    selva::combat::combatLog("[hit-react] enemy[{}] dmg={} poise={:.1f}/{:.1f} -> clip={}", index,
                             damage, e.poise.current, e.poise.max, pick.clip_name);
}

} // namespace selva::gameplay
