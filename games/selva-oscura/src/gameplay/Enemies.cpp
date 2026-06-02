#include "gameplay/Enemies.h"

#include "AppStateGlobal.h"
#include "Formulas.h"
#include "Scene.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletonJointMap.h"
#include "audio/Audio.h"
#include "combat/ActorVolumes.h"
#include "combat/CombatLog.h"
#include "combat/HitVolumes.h"
#include "gameplay/AiBarriers.h"
#include "gameplay/AiTick.h"
#include "gameplay/BehaviorTree.h"
#include "gameplay/BossRewards.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/Perception.h"
#include "world/Collision.h"
#include "world/Terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace selva::gameplay
{

namespace
{

// Severity tier + cooldown + respawn knobs live in Tunables so
// they're hot-reloadable from the F1 panel and serialize with the
// rest of the game's feel parameters. Read fresh each call.

constexpr float kEnemyWalkSpeedFloor = 0.15f; // m/s; above = walk, below = idle
constexpr float kEnemyRunSpeedFloor = 3.0f;    // m/s; above = Run family, below = Walk family

// Per-archetype clip family. Every animation lookup for an enemy
// flows through lookupArchetypeClip(actor, family) -- the ONLY path
// from gameplay code to a ClipRegistry. The function (a) reads the
// archetype's override field for the family, falling back to the
// humanoid default key, then (b) looks up the resulting key in the
// actor's OWN skeleton clip registry (clipsByKey(actor.skeleton_id)),
// NOT the player registry. This is load-bearing: feeding a clip
// authored against skeleton A (e.g. player, 65 joints) to a sampler
// bound to skeleton B (e.g. wolf, 53 joints) makes ozz's SamplingJob
// write into uninit SoaTransform lanes, and the next LocalToModelJob
// hits IsNormalizedEst on garbage memory. See [[feedback_data_driven_over_convention]].
enum class ClipFamily
{
    PeacefulIdle,
    CombatIdle,
    Walk,
    Run,
    WalkBack,
    StrafeLeft,
    StrafeRight,
    Death,
    Knockdown,
    FlinchFront,
    FlinchBack,
    FlinchLeft,
    FlinchRight,
    HitReactMedium,
    HitReactHeavy,
};

// Resolve a family to (archetype override OR humanoid default) key.
// Pure string mapping -- no registry access.
const char* clipFamilyDefaultKey(ClipFamily fam)
{
    switch (fam)
    {
    case ClipFamily::PeacefulIdle: return "standard_idle";
    case ClipFamily::CombatIdle: return "unarmed_combat_idle";
    case ClipFamily::Walk: return "walking";
    case ClipFamily::Run: return "running";
    case ClipFamily::WalkBack: return "walking_backward";
    case ClipFamily::StrafeLeft: return "strafe_walking_left";
    case ClipFamily::StrafeRight: return "strafe_walking_right";
    case ClipFamily::Death: return "death";
    case ClipFamily::Knockdown: return "stunned";
    case ClipFamily::FlinchFront: return "flinch_front";
    case ClipFamily::FlinchBack: return "flinch_back";
    case ClipFamily::FlinchLeft: return "flinch_left";
    case ClipFamily::FlinchRight: return "flinch_right";
    case ClipFamily::HitReactMedium: return "hit_react_medium";
    case ClipFamily::HitReactHeavy: return "hit_react_heavy";
    }
    return "";
}

const std::string& clipFamilyArchetypeOverride(const EnemyArchetype& arch, ClipFamily fam)
{
    static const std::string kEmpty;
    switch (fam)
    {
    case ClipFamily::PeacefulIdle: return arch.idle_clip;
    case ClipFamily::CombatIdle: return arch.combat_idle_clip;
    case ClipFamily::Walk: return arch.walk_clip;
    case ClipFamily::Run: return arch.run_clip;
    case ClipFamily::WalkBack: return arch.walk_back_clip;
    case ClipFamily::StrafeLeft: return arch.strafe_left_clip;
    case ClipFamily::StrafeRight: return arch.strafe_right_clip;
    case ClipFamily::Death: return arch.death_clip;
    case ClipFamily::Knockdown: return arch.knockdown_clip;
    case ClipFamily::FlinchFront: return arch.flinch_front_clip;
    case ClipFamily::FlinchBack: return arch.flinch_back_clip;
    case ClipFamily::FlinchLeft: return arch.flinch_left_clip;
    case ClipFamily::FlinchRight: return arch.flinch_right_clip;
    case ClipFamily::HitReactMedium: return arch.hit_react_medium_clip;
    case ClipFamily::HitReactHeavy: return arch.hit_react_heavy_clip;
    }
    return kEmpty;
}

struct ClipLookup
{
    const selva::anim::AnimationClip* clip;
    const char* key; // pointer into archetype string or default literal; valid for actor's lifetime
};

// SINGLE funnel from gameplay code to an enemy's animation clip.
// Resolves family -> archetype override (if any) -> default key, then
// reads from the actor's per-skeleton registry. Never touches the
// global player registry. Returns {nullptr, ""} if the actor has no
// archetype or the resolved key isn't loaded for the skeleton.
ClipLookup lookupArchetypeClip(const Actor& a, ClipFamily fam)
{
    const EnemyArchetype* arch = a.archetype;
    const char* key = clipFamilyDefaultKey(fam);
    if (arch != nullptr)
    {
        const std::string& override_key = clipFamilyArchetypeOverride(*arch, fam);
        if (!override_key.empty())
            key = override_key.c_str();
    }
    const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
    const auto* c = reg.get(key);
    return {c, key};
}

} // namespace (close anon ns; spawnEnemyFromDecl is declared in the
  // header as selva::gameplay::spawnEnemyFromDecl so it must NOT be in
  // an anon namespace -- otherwise both this anon-ns function and the
  // public one are ambiguous within this TU)

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
    // Boss lifecycle gates. Resolved BEFORE allocating the actor so a
    // skipped boss does not consume a pool slot.
    //
    // 1) Felled-boss check: if this is a boss whose spawn-decl id is
    //    in the active profile's felled_bosses, skip permanently for
    //    this save. Per [[selva-wood-lore-locked-2026-05-31]] the
    //    empty slope IS the monument.
    //
    // 2) Trigger-spawn skip: if spawn_trigger_id is non-empty, this
    //    enemy spawns on trigger (Pattern A), not at boot. Skipped
    //    here; spawned later when the custom trigger fires.
    //
    // Pattern B bosses (engage_trigger_id set, spawn_trigger_id
    // empty) DO boot-spawn but enter their initial_state below.
    const EnemyArchetype* archetype_ptr =
        decl.archetype.empty() ? nullptr : archetypes().get(decl.archetype);
    if (archetype_ptr != nullptr && archetype_ptr->is_boss)
    {
        const PlayerProfile* profile = selva::activePlayerProfile();
        if (profile != nullptr)
        {
            for (const auto& felled_id : profile->felled_bosses)
            {
                if (felled_id == decl.id)
                {
                    selva::combat::combatLog(
                        "[spawn] boss '{}' skipped at boot (already felled this save)",
                        decl.id);
                    return;
                }
            }
        }
    }
    if (!decl.spawn_trigger_id.empty())
    {
        // Trigger-spawned: skip at boot. The custom-trigger dispatcher
        // will call spawnEnemyFromDecl again when the trigger fires.
        selva::combat::combatLog(
            "[spawn] enemy '{}' deferred (waits on spawn_trigger '{}')", decl.id,
            decl.spawn_trigger_id);
        return;
    }

    Actor e;
    e.controller = Controller::AI_Stationary;
    // Faction sourced from archetype (default Hostile if archetype
    // didn't specify, matching the legacy hardcoded value). NPCs +
    // future companions override to Allied / Neutral in their JSON.
    e.faction = (archetype_ptr != nullptr) ? archetype_ptr->faction : Faction::Hostile;
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
    // Bind archetype FIRST -- sampler creation below reads skeleton_id
    // from the archetype, and previously we were reading e.archetype
    // (still null at this point in the Actor's lifetime). Bug
    // symptom: every non-player actor got a sampler bound to the
    // player rig regardless of archetype.skeleton_id, which caused
    // an ozz IsNormalizedEst quaternion assertion on first sample
    // (wrong joint indices into the wrong skeleton).
    if (!decl.archetype.empty())
    {
        e.archetype = (archetype_ptr != nullptr) ? archetype_ptr : archetypes().get(decl.archetype);
        if (e.archetype == nullptr)
            selva::combat::combatLog("[spawn] archetype '{}' not found in registry (id='{}')",
                                     decl.archetype, e.spawn_id);
    }
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
    // Mirror form from archetype (default DamnedSoul; existing shade
    // JSON omits the field). Apply per-form Body/Stats DEFAULTS before
    // initActorPools so the pools derive from form-baseline numbers.
    // Per-archetype overrides (max_hp_override etc.) layer on AFTER
    // pool init.
    e.form = (e.archetype != nullptr) ? e.archetype->form : Form::DamnedSoul;
    applyFormDefaults(e.body, e.stats, e.form);
    initActorPools(e.hp, e.stamina, e.poise, e.body, e.stats);
    // Per-archetype pool overrides for boss-tier actors. Lupa: legend
    // HP, broken poise (her stagger easily but she TANKS hits).
    if (e.archetype != nullptr)
    {
        if (e.archetype->max_hp_override > 0)
        {
            e.hp.max = e.archetype->max_hp_override;
            e.hp.current = e.hp.max;
        }
        if (e.archetype->max_poise_override > 0.0f)
        {
            e.poise.max = e.archetype->max_poise_override;
            e.poise.current = e.poise.max;
        }
        std::fprintf(stderr,
                     "[spawn] '%s' archetype='%s' form=%s hp.max=%d poise.max=%.1f "
                     "(overrides: hp=%d poise=%.1f)\n",
                     e.spawn_id.c_str(), decl.archetype.c_str(), formName(e.form), e.hp.max,
                     e.poise.max, e.archetype->max_hp_override,
                     e.archetype->max_poise_override);
        std::fflush(stderr);
    }
    // Seed per-actor RNG. Two actors of the same archetype get
    // independent rolls so they don't synchronize their weighted
    // action picks. random_device + a salt from spawn pos makes
    // co-spawned actors diverge on the first call.
    std::random_device rd;
    e.rng.seed(rd() ^ static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.x * 1000.0f)) ^
               static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.z * 1000.0f)));
    // Skeleton: archetype's id (default "player"). Archetype was
    // bound above (before sampler creation). Resolved by
    // selva::anim::meshByKey / skeletonByKey at use sites.
    e.skeleton_id = (e.archetype != nullptr) ? e.archetype->skeleton_id : std::string("player");
    // Boss fields. Mirror archetype.is_boss for hot-path lookups.
    // boss_state init follows: Pattern B (is_boss + non-empty
    // initial_state) -> Dormant (the explicit setBossState call below
    // writes the legacy current_boss_state mirror); Pattern A and
    // non-bosses leave boss_state at default Dormant, which is
    // semantically meaningless for them (nothing reads it).
    e.is_boss = (e.archetype != nullptr) && e.archetype->is_boss;
    e.is_npc = (e.archetype != nullptr) && e.archetype->is_npc;
    e.spawn_decl_id = decl.id;
    e.boss_state = BossState::Dormant; // default; setBossState below writes mirrors if Pattern B
    e.current_boss_state.clear();      // setBossState(Dormant) will re-write from archetype if needed
    // Prime the sampler with the peaceful-idle clip as the LOCO track.
    // Resolved via the single archetype->per-skeleton-registry funnel
    // (lookupArchetypeClip) so the wolf's sampler binds to a wolf clip,
    // not the player's.
    const ClipLookup spawn_idle = lookupArchetypeClip(e, ClipFamily::PeacefulIdle);
    if (spawn_idle.clip != nullptr && spawn_idle.clip->isLoaded())
    {
        e.sampler.update(*spawn_idle.clip, 0.0f, 0.0f);
        // Pattern B held-pose: if the archetype declares a freeze
        // timestamp, fire idle_clip as a freeze-held one-shot on top
        // of the loco track. Visually the actor lands and stays at
        // the chosen mid-clip frame (Lupa: 1.69s of idle_2_head_low
        // = the perfect sad sit). On engage, engage_clip starts as
        // a fresh one-shot and crossfades over the held pose.
        if (e.archetype != nullptr && e.archetype->initial_freeze_at_seconds > 0.0f &&
            !e.archetype->initial_state.empty())
        {
            selva::anim::PoseSampler::OneShotOptions opts;
            opts.clip_key = spawn_idle.key;
            opts.freeze_last = true;
            opts.freeze_at_seconds = e.archetype->initial_freeze_at_seconds;
            // start_time_seconds = freeze_at_seconds: clip begins AT
            // the freeze frame so the held pose is visible from frame
            // one (otherwise the clip plays 0->1.69 first, then holds).
            e.sampler.playOneShot(*spawn_idle.clip, /*blend_in_seconds=*/0.0f,
                                  /*blend_out_seconds=*/0.20f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time_seconds=*/e.archetype->initial_freeze_at_seconds,
                                  /*playback_rate=*/1.0f, opts);
        }
    }
    else
        selva::combat::combatLog(
            "[spawn] no idle clip '{}' on skeleton '{}' (actor '{}')", spawn_idle.key,
            e.skeleton_id, e.spawn_id);
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
    const bool needs_dormant_init =
        (e.archetype != nullptr) && e.archetype->is_boss &&
        !e.archetype->initial_state.empty();
    actors().push_back(std::move(e));
    if (needs_dormant_init)
    {
        // Force a setBossState(Dormant) on the pooled actor so the
        // mirror writes (current_boss_state = initial_state, etc.) run
        // through the single funnel. Default boss_state is already
        // Dormant -- but setBossState skips no-op transitions, so we
        // need to temporarily nudge to a different state first.
        Actor& pooled = actors()[pool_index];
        pooled.boss_state = BossState::Engaged; // sentinel != Dormant
        setBossState(pooled, BossState::Dormant);
    }
}

namespace
{ // re-open anon ns for the rest of the helpers below

// Pick the directional flinch family from the hit normal rotated into
// the target's local frame. Convention: target yaw=0 faces -Z.
// Local forward = -Z; local right = +X.
//
// world_normal points attacker->target (impact impulse direction).
// The side STRUCK is the side facing the attacker, i.e. -world_normal.
// We rotate -world_normal into target-local space and pick the family
// from the dominant axis. Returns a ClipFamily; caller resolves to a
// concrete clip via lookupArchetypeClip.
ClipFamily pickDirectionalFlinchFamily(float target_yaw, const glm::vec3& world_normal)
{
    const glm::vec3 strike_dir = -world_normal; // target -> attacker
    const float cy = std::cos(-target_yaw);
    const float sy = std::sin(-target_yaw);
    const float local_x = cy * strike_dir.x + sy * strike_dir.z;
    const float local_z = -sy * strike_dir.x + cy * strike_dir.z;
    if (std::abs(local_x) > std::abs(local_z))
        return (local_x > 0.0f) ? ClipFamily::FlinchRight : ClipFamily::FlinchLeft;
    return (local_z < 0.0f) ? ClipFamily::FlinchFront : ClipFamily::FlinchBack;
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
    // Boss-backend Pattern B gate: if the boss is still in its
    // initial_state (e.g. Lupa sitting), DO NOT tick combat
    // decisions. The actor holds its idle pose until the engage
    // trigger fires (which clears current_boss_state via
    // BossDispatcher). See docs/design/ideas/boss_backend.md
    // section 6 + lifecycle step 3.
    if (a.is_boss && !a.current_boss_state.empty())
        return;
    if (a.boss_state == BossState::Dying)
        return;
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
    // RootMotion gate — mirrors tickPlayerVelocity. If the actor's
    // current loco clip drives world translation via consumedHipDelta
    // (applyActorClipHipDelta at the end of tickOneEnemy), velocity
    // integration must NOT also run or the two compound (visible foot-
    // skate / "moves at 2x speed"). Read the active clip's source from
    // the global locomotion config; default Velocity for unregistered
    // clips. Wolf walk + gallop are root_motion per config/locomotion.json
    // so wolf gait pace == authored cadence, no per-archetype walk_speed
    // tuning. Same contract as the human directional gait clips.
    const auto fd = a.sampler.frameDiagnostics();
    const char* cur_name = fd.loco_current_name;
    const selva::anim::TranslationSource src =
        (cur_name != nullptr)
            ? selva::anim::locomotionConfig().translationSource(cur_name)
            : selva::anim::TranslationSource::Velocity;
    const bool root_motion_clip = (src == selva::anim::TranslationSource::RootMotion);

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
    // Movement-lock rules:
    //   * action_locks_movement (EnemyAction.locks_movement=true):
    //     velocity zeroed for FULL one-shot duration. Cancel_fraction
    //     governs other things (chain-input) but not locomotion. Heavy
    //     swings where the body MUST stay planted (wolf bite).
    //   * Else cancel_fraction: zero only until past cancel-fraction;
    //     after that, velocity ramp resumes WHILE recovery animation
    //     plays. Light/tracking attacks where the actor should
    //     reposition during recovery.
    //   * Root-motion loco clip: always zero (clip drives translation,
    //     velocity must not also integrate).
    const bool one_shot_active = a.sampler.isOneShotActive();
    const bool hard_lock = one_shot_active && a.action_locks_movement;
    const bool soft_lock = one_shot_active && !a.sampler.isOneShotPastCancelFraction();
    const bool gated = hard_lock || soft_lock || root_motion_clip;
    // Clear the per-action lock once the one-shot ends so the next
    // action starts from a clean slate.
    if (!one_shot_active)
        a.action_locks_movement = false;
    if (gated)
    {
        a.velocity_xz = glm::vec2(0.0f);
        // Yaw still ticks below (turn-while-rooted is correct -- the
        // wolf rotates her body to face the player while authored
        // walk drives forward translation).
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
    const ClipLookup knockdown = lookupArchetypeClip(e, ClipFamily::Knockdown);
    if (knockdown.clip != nullptr && knockdown.clip->isLoaded())
    {
        selva::anim::PoseSampler::OneShotOptions opts;
        opts.clip_key = knockdown.key;
        opts.freeze_last = true;
        const float start_t = std::max(0.0f, tun.knockdown_clip_start_seconds);
        e.sampler.playOneShot(*knockdown.clip, /*blend_in_seconds=*/0.25f,
                              /*blend_out_seconds=*/0.25f, selva::anim::PoseSampler::BodyMask::Full,
                              start_t,
                              /*playback_rate=*/1.0f, opts);
        selva::combat::combatLog(
            "[knockdown] firing knockdown clip dur={:.3f}s start={:.2f}s end_tunable={:.2f}s",
            knockdown.clip->duration(), start_t, tun.knockdown_clip_end_seconds);
    }
    e.is_knocked_down = true;
    e.knockdown_start_time = now;
    e.poise.current = e.poise.max; // reset on break
    e.last_damage_time = now;
    selva::combat::combatLog(
        "[knockdown] enemy[{}] poise broke (dmg={} poise_dmg={}) -> knockdown clip", index, damage,
        poise_damage);
}

// Pick the hit-react clip family + blend timings from damage tier.
// Sub-medium damage falls through to a directional flinch family.
// Caller resolves the family to a concrete clip via lookupArchetypeClip
// so the per-skeleton registry is honored.
struct HitReactPick
{
    ClipFamily family;
    float blend_in;
    float blend_out;
};
HitReactPick pickHitReactFamily(int damage, float target_yaw, const glm::vec3& world_normal)
{
    const auto& tun = selva::tuning::current();
    if (static_cast<float>(damage) >= tun.hit_react_heavy_threshold)
        return {ClipFamily::HitReactHeavy, 0.08f, 0.20f};
    if (static_cast<float>(damage) >= tun.hit_react_medium_threshold)
        return {ClipFamily::HitReactMedium, 0.06f, 0.15f};
    return {pickDirectionalFlinchFamily(target_yaw, world_normal), 0.04f, 0.15f};
}

} // namespace

// Public — exposed so the PC's death path in PerFrameTick can fire
// the same one-shot routing as enemies do. Reads `e.death_clip_name`
// so each actor controls its own visual; the PC's is "second_death".
void fireEnemyDeath(Actor& e, int index)
{
    // Per-actor override (e.death_clip_name) wins so the player ("second_death")
    // keeps working without an archetype. For enemies whose death_clip_name
    // is unset OR points to a key that doesn't exist on the actor's
    // skeleton, fall through to the archetype's Death family on the
    // per-skeleton registry. The player path uses selva::anim::clips()
    // (skel="player"); enemy actors use clipsByKey(skeleton_id).
    const auto& reg = selva::anim::clipsByKey(e.skeleton_id);
    const selva::anim::AnimationClip* death_clip = nullptr;
    const char* clip_name = "";
    if (!e.death_clip_name.empty())
    {
        const auto* c = reg.get(e.death_clip_name);
        if (c != nullptr && c->isLoaded())
        {
            death_clip = c;
            clip_name = e.death_clip_name.c_str();
        }
    }
    if (death_clip == nullptr)
    {
        const ClipLookup fallback = lookupArchetypeClip(e, ClipFamily::Death);
        death_clip = fallback.clip;
        clip_name = fallback.key;
    }
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
    // Boss-death save-persistence (per
    // docs/design/ideas/boss_backend.md section 5): append the
    // boss's spawn_decl_id to the active profile's felled_bosses
    // list, clear GameState.active_boss_* so the GUI / music / arena
    // lockout fall back to ambient. The boss-felled overlay (step 9)
    // hooks the same edge by polling active_boss_idx transitioning
    // from valid to -1.
    if (e.is_boss && !e.spawn_decl_id.empty())
    {
        // SINGLE source of truth: setBossState(Felled) clears
        // current_boss_state, active_boss_idx/id, pops the audio bed,
        // logs the transition. Profile-write + reward dispatch happen
        // here on the death edge regardless of prior state.
        setBossState(e, BossState::Felled);
        PlayerProfile* profile = selva::activePlayerProfile();
        if (profile != nullptr)
        {
            // Idempotent: don't double-append if already felled
            // (shouldn't happen since spawn-skip excludes felled
            // bosses, but defensive).
            const auto& fb = profile->felled_bosses;
            if (std::find(fb.begin(), fb.end(), e.spawn_decl_id) == fb.end())
            {
                profile->felled_bosses.push_back(e.spawn_decl_id);
                selva::combat::combatLog(
                    "[boss-felled] '{}' appended to profile '{}'.felled_bosses",
                    e.spawn_decl_id, profile->name);
            }
        }
        // Reward dispatch. For v1 we assume the player killed the boss
        // (true today; faction-conflict will refine the attribution
        // later by routing through hit-detection's owner field).
        selva::gameplay::onBossFelled(e.spawn_decl_id, selva::gameplay::player());
    }
    if (e.archetype != nullptr && !e.archetype->felled_flag.empty())
        setFlag(e.archetype->felled_flag);
    // Scripted-death actors open the cinematic Scene at the moment the
    // death clip fires (not earlier at Dying), so the player can keep
    // attacking through the pain stage.
    if (e.archetype != nullptr && e.archetype->scripted_death_seconds > 0.0f &&
        !selva::scene::active())
    {
        selva::scene::begin({/*combat=*/true, /*movement=*/true, /*look=*/false});
    }
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
    // Phase 1: every AI actor resets to JSON-default alive baseline.
    // Cross-character state (a corpse left from the prior profile)
    // must not leak. The profile-felled reconciliation in phase 2 is
    // the only source of "stays dead" for the new session.
    auto& pool = actors();
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;

        a.pos = a.spawn_pos;
        a.yaw = a.spawn_yaw;
        a.velocity_xz = glm::vec2(0.0f);
        a.intent_xz = glm::vec2(0.0f);
        a.turn_intent_yaw = a.spawn_yaw;

        initActorPools(a.hp, a.stamina, a.poise, a.body, a.stats);

        a.is_dead = false;
        a.death_time = -1.0f;
        a.is_knocked_down = false;
        a.knockdown_start_time = -1.0f;
        a.last_damage_time = -1.0f;
        a.last_hit_react_time = -1.0f;

        // Boss state to Dormant. Nudge through a non-Dormant sentinel
        // when already Dormant so the mirror writes (audio bed pop,
        // active_boss_* clear) fire even on no-op transitions.
        if (a.is_boss && a.boss_state != BossState::Dormant)
        {
            setBossState(a, BossState::Dormant);
        }
        else if (a.is_boss)
        {
            a.boss_state = BossState::Engaged;
            setBossState(a, BossState::Dormant);
        }
        a.scripted_death_at_wallclock = -1.0f;
        a.scripted_death_drain_end_wallclock = -1.0f;
        a.dying_until_wallclock = -1.0f;

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
        const ClipLookup reset_idle = lookupArchetypeClip(a, ClipFamily::PeacefulIdle);
        if (reset_idle.clip != nullptr && reset_idle.clip->isLoaded())
            a.sampler.update(*reset_idle.clip, 0.0f, 0.0f);
    }

    // Phase 2: re-apply the active profile's felled_bosses on top of
    // the freshly-reset world. Idempotent fireEnemyDeath puts each
    // felled boss into its death pose + Felled state.
    const PlayerProfile* profile = selva::activePlayerProfile();
    if (profile != nullptr)
    {
        for (auto& a : pool)
        {
            if (a.controller == Controller::Input)
                continue;
            if (!a.permanent_on_death || a.spawn_decl_id.empty())
                continue;
            const auto& fb = profile->felled_bosses;
            if (std::find(fb.begin(), fb.end(), a.spawn_decl_id) == fb.end())
                continue;
            std::fprintf(stderr,
                         "[reset-cycle] '%s' in profile.felled_bosses -> fireEnemyDeath\n",
                         a.spawn_decl_id.c_str());
            std::fflush(stderr);
            fireEnemyDeath(a, enemyIndex(a));
        }
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

// Map directionalLocoClip's humanoid-default key string back to a
// ClipFamily so the enemy path can re-resolve it through the per-
// skeleton registry. Returns nullopt on unknown / non-locomotion
// strings (player-only keys like "running_backward" -- enemies don't
// sprint).
std::optional<ClipFamily> familyFromDirectionalKey(const char* k)
{
    if (k == nullptr)
        return std::nullopt;
    std::string s(k);
    if (s == "walking") return ClipFamily::Walk;
    if (s == "walking_backward") return ClipFamily::WalkBack;
    if (s == "strafe_walking_left") return ClipFamily::StrafeLeft;
    if (s == "strafe_walking_right") return ClipFamily::StrafeRight;
    return std::nullopt;
}

// Pick this frame's locomotion clip for an enemy. Three states by
// priority: walking > combat-idle > peaceful-idle. When locked +
// moving, the shared PC/NPC directional picker chooses a strafe /
// back variant. Every lookup funnels through lookupArchetypeClip so
// the wolf's sampler binds to wolf clips, never the player's.
struct EnemyLocoPick
{
    const selva::anim::AnimationClip* clip;
    const char* key;
};
static EnemyLocoPick pickEnemyLocomotionClip(const Actor& a)
{
    const ClipLookup walk = lookupArchetypeClip(a, ClipFamily::Walk);
    const ClipLookup run = lookupArchetypeClip(a, ClipFamily::Run);
    const ClipLookup combat_idle = lookupArchetypeClip(a, ClipFamily::CombatIdle);
    const ClipLookup peaceful_idle = lookupArchetypeClip(a, ClipFamily::PeacefulIdle);
    // Speed read: prefer velocity when the actor moves via velocity
    // integration; substitute intent magnitude when the current loco
    // clip is RootMotion (velocity is zeroed by tickEnemyLocomotion in
    // that case, so the gait picker would never see motion). Mirrors
    // the sLastTargetSpeed pattern in tickPlayerVelocity. Without this,
    // root-motion gait actors get stuck in idle.
    const float speed = std::max(glm::length(a.velocity_xz), glm::length(a.intent_xz));
    const bool is_moving = (speed > kEnemyWalkSpeedFloor);
    const bool is_running =
        is_moving && (speed >= kEnemyRunSpeedFloor) && run.clip != nullptr && run.clip->isLoaded();
    const bool is_walking = is_moving && walk.clip != nullptr && walk.clip->isLoaded();
    const bool engaged = a.perception.awareness >= Awareness::Alerted;
    const ClipLookup base = is_running ? run
                            : is_walking ? walk
                                         : (engaged ? combat_idle : peaceful_idle);
    // Diagnostic: log gait picker decisions for wolf-skeleton actors.
    // Throttled by frame counter (every 30th call ~ 2 Hz at 60fps to
    // avoid log spam). Tells us why "run never fires" -- if speed is
    // 1.6 m/s when it should be 6, the chase_speed isn't winning; if
    // speed is 6 but run.clip is null, the lookup fails; if both OK
    // and is_running=1 then the issue is downstream in PoseSampler.
    if (a.skeleton_id == "wolf")
    {
        static int s_tick = 0;
        if ((++s_tick % 30) == 0)
        {
            std::fprintf(stderr,
                         "[gait-picker] wolf speed=%.2f (vel=%.2f intent=%.2f) floor_walk=%.2f "
                         "floor_run=%.2f run.clip=%s walk.clip=%s -> is_running=%d is_walking=%d "
                         "picked='%s'\n",
                         speed, glm::length(a.velocity_xz), glm::length(a.intent_xz),
                         kEnemyWalkSpeedFloor, kEnemyRunSpeedFloor,
                         (run.clip != nullptr) ? "yes" : "NULL",
                         (walk.clip != nullptr) ? "yes" : "NULL", is_running ? 1 : 0,
                         is_walking ? 1 : 0, base.key ? base.key : "(none)");
            std::fflush(stderr);
        }
    }
    EnemyLocoPick out{base.clip, base.key};
    if (!is_moving || a.lock_target_idx < 0)
        return out;
    // Directional override: shared with PC via directionalLocoClip.
    // Basis from turn_intent_yaw (target facing) not actor.yaw -- yaw
    // lags by turn rate and would thrash the picker frame-to-frame.
    //
    // ONLY override when intent has meaningful lateral component (i.e.,
    // actor is actually strafing or backing up). Pure forward intent
    // keeps the base pick (run/walk straight at target). Without this
    // gate, a chasing wolf would be downgraded run -> walk every frame
    // because directionalLocoClip only returns walk-family keys and
    // never the run family -- the override would always replace the
    // base 'gallop' pick with 'walk'. Bug surfaced when chase_speed
    // landed and the wolf STILL refused to play her gallop clip.
    const float yaw = a.turn_intent_yaw;
    const glm::vec3 fwd(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    const glm::vec3 intent3(a.intent_xz.x, 0.0f, a.intent_xz.y);
    const float intent_len = glm::length(intent3);
    if (intent_len > 1e-4f)
    {
        const glm::vec3 intent_dir = intent3 / intent_len;
        const float fwd_dot = glm::dot(intent_dir, fwd);
        constexpr float kForwardishDot = 0.85f; // ~32deg cone = "forward"
        if (fwd_dot >= kForwardishDot)
            return out; // pure-forward chase: keep base (run/walk) pick
    }
    const char* dir_key = directionalLocoClip(fwd, right, intent3, /*running=*/false);
    const auto fam = familyFromDirectionalKey(dir_key);
    if (!fam.has_value())
        return out;
    const ClipLookup dir = lookupArchetypeClip(a, *fam);
    if (dir.clip != nullptr && dir.clip->isLoaded())
    {
        out.clip = dir.clip;
        out.key = dir.key;
    }
    return out;
}

// Per-actor body of tickEnemies. Returns true if the lifecycle
// short-circuited (death/knockdown handled the actor this frame and
// the locomotion path should be skipped). All clip lookups go
// through the per-skeleton funnel -- the wolf's sampler never sees a
// player clip.
static bool tickOneEnemy(Actor& a, const Actor& pc, float dt, const selva::tuning::Tunables& tun)
{
    // Per-frame motion stream for the wolf -- unthrottled so we can see
    // every frame's pos/vel/yaw. If pos jumps non-linearly between
    // frames OR yaw jolts at the AI tick rate (5Hz), the stutter is
    // in tickEnemyLocomotion or the BT cadence. If pos/yaw are smooth
    // but the visible mesh still jitters, it's rendering/camera side.
    glm::vec3 prev_pos_for_log = a.pos;
    float prev_yaw_for_log = a.yaw;
    glm::vec2 prev_vel_for_log = a.velocity_xz;
    bool log_this_frame = (a.skeleton_id == "wolf");

    tickPerception(a, pc, dt, tun);
    updateEnemyLockOnPlayer(a, pc);
    const bool engaged_now = a.perception.awareness >= Awareness::Alerted;
    const ClipLookup lifecycle_idle =
        lookupArchetypeClip(a, engaged_now ? ClipFamily::CombatIdle : ClipFamily::PeacefulIdle);
    if (tickDeathLifecycle(a, dt, lifecycle_idle.clip, lifecycle_idle.key))
    {
        applyActorClipHipDelta(a);
        return true;
    }
    if (tickKnockdownLifecycle(a, dt, lifecycle_idle.clip, lifecycle_idle.key))
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
    const EnemyLocoPick pick = pickEnemyLocomotionClip(a);
    if (pick.clip != nullptr && pick.clip->isLoaded())
        a.sampler.update(*pick.clip, dt, /*blend_seconds=*/0.20f, /*loops=*/true, pick.key);
    applyActorClipHipDelta(a);
    // Wolf one-shot / boss-state diagnostic. Fires regardless of state
    // so we can see whether is_boss + initial_state actually apply
    // and whether the held-pose one-shot is alive.
    if (log_this_frame)
    {
        static int s_sit_tick = 0;
        if ((++s_sit_tick % 60) == 0) // ~1 Hz
        {
            const auto fd = a.sampler.frameDiagnostics();
            std::fprintf(stderr,
                         "[wolf-state] is_boss=%d boss_state='%s' awareness=%d "
                         "loco='%s' loco_t=%.2f one_shot='%s' one_shot_phase=%d "
                         "one_shot_weight=%.2f one_shot_t=%.2f\n",
                         a.is_boss ? 1 : 0, a.current_boss_state.c_str(),
                         static_cast<int>(a.perception.awareness),
                         fd.loco_current_name ? fd.loco_current_name : "(none)",
                         fd.loco_current_time,
                         fd.one_shot_name ? fd.one_shot_name : "(none)", fd.one_shot_phase,
                         fd.one_shot_weight, fd.one_shot_time);
            std::fflush(stderr);
        }
    }
    if (log_this_frame && a.perception.awareness >= Awareness::Combat)
    {
        const glm::vec3 dpos = a.pos - prev_pos_for_log;
        const float dpos_mag = glm::length(glm::vec2(dpos.x, dpos.z));
        const float dyaw = a.yaw - prev_yaw_for_log;
        const float dvel = glm::length(a.velocity_xz - prev_vel_for_log);
        const bool one_shot = a.sampler.isOneShotActive();
        const auto fd = a.sampler.frameDiagnostics();
        std::fprintf(stderr,
                     "[wolf-motion] dt=%.4f pos=(%.3f,%.3f) dpos=%.4fm vel=(%.2f,%.2f) "
                     "|dvel|=%.3f yaw=%.3f intent=(%.2f,%.2f) loco='%s' oneshot=%d "
                     "one_shot_clip='%s'\n",
                     dt, a.pos.x, a.pos.z, dpos_mag, a.velocity_xz.x, a.velocity_xz.y, dvel,
                     a.yaw, a.intent_xz.x, a.intent_xz.y, pick.key ? pick.key : "(none)",
                     one_shot ? 1 : 0, fd.one_shot_name ? fd.one_shot_name : "(none)");
        std::fflush(stderr);
    }
    return false;
}

void beginScriptedDying(Actor& a)
{
    if (a.boss_state != BossState::Engaged)
        return;
    if (a.archetype == nullptr || a.archetype->scripted_death_seconds <= 0.0f)
        return;
    const float now = selva::wallClock();
    const std::string& pain_key = a.archetype->scripted_death_pain_clip;
    float pain_duration = 0.0f;
    if (!pain_key.empty())
    {
        const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
        const auto* clip = reg.get(pain_key);
        if (clip != nullptr && clip->isLoaded())
        {
            pain_duration = clip->duration();
            selva::anim::PoseSampler::OneShotOptions opts;
            opts.clip_key = pain_key.c_str();
            opts.freeze_last = true;
            a.sampler.playOneShot(*clip, /*blend_in_seconds=*/0.0f,
                                  /*blend_out_seconds=*/0.0f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
        }
        else
        {
            std::fprintf(stderr,
                         "[scripted-death] '%s' pain clip '%s' missing on skeleton '%s'; "
                         "skipping pain stage\n",
                         a.spawn_decl_id.c_str(), pain_key.c_str(), a.skeleton_id.c_str());
            std::fflush(stderr);
        }
    }
    a.dying_until_wallclock = now + pain_duration;
    a.intent_xz = glm::vec2(0.0f);
    a.velocity_xz = glm::vec2(0.0f);
    setBossState(a, BossState::Dying);
}

// Per-frame promoter for deferred (windup-modeled) attack hitboxes.
// LeafPickAction::fireAction queues a PendingAttackSpawn on the actor
// when the action declares windup_seconds > 0. This walks every actor
// (player + enemies) and promotes any pending whose fire_at_time has
// elapsed -- visually that's the END of the windup, START of the
// active window. Hitbox lifetime = action.active_seconds, then it
// despawns via the normal tickHitboxes lifecycle.
//
// Called once per frame from tickEnemies. Player path can hook the
// same mechanism later when player attacks adopt the windup schema.
void tickPendingAttackSpawns()
{
    const float now = selva::wallClock();
    for (auto& a : actors())
    {
        if (a.pending_attack.fire_at_time < 0.0f)
            continue;
        if (a.boss_state == BossState::Dying)
        {
            a.pending_attack.fire_at_time = -1.0f;
            continue;
        }
        if (now < a.pending_attack.fire_at_time)
            continue;
        // Promote.
        selva::combat::AttackHitboxSpawnParams sp;
        sp.actor = &a;
        sp.attacker = selva::combat::OwnerRef{
            (a.controller == Controller::Input) ? selva::combat::OwnerKind::Player
                                                : selva::combat::OwnerKind::Enemy,
            (a.controller == Controller::Input) ? 0 : enemyIndex(a)};
        sp.attacker_faction = a.faction;
        sp.raw_damage = a.pending_attack.raw_damage;
        sp.poise_damage = a.pending_attack.poise_damage;
        sp.joint_name = a.pending_attack.joint_name.c_str();
        sp.hitbox_radius = a.pending_attack.radius;
        sp.hitbox_tip_offset_z = a.pending_attack.tip_offset_z;
        // lifetime_fraction = 1.0 + clip_duration_seconds = lifetime
        // makes spawnAttackHitbox emit a hitbox with the exact authored
        // active-window length. Bypasses the legacy duration*0.55 calc.
        sp.clip_duration_seconds = a.pending_attack.lifetime_seconds;
        sp.clip_start_seconds = 0.0f;
        sp.playback_rate = 1.0f;
        sp.lifetime_fraction = 1.0f;
        sp.mesh_foot_offset_y = selva::gameplay::actorFootOffsetY(a);
        selva::combat::spawnAttackHitbox(sp);
        a.pending_attack.fire_at_time = -1.0f; // consumed
    }
}

void tickEnemies(float dt)
{
    const auto& tun = selva::tuning::current();
    const Actor& pc = player();
    for (auto& a : actors())
    {
        if (a.controller == Controller::Input)
            continue;
        tickOneEnemy(a, pc, dt, tun);
    }
    tickPendingAttackSpawns();
    // Scripted-death watchers. Two phases:
    //   1. Engaged -> Dying. Triggered either by natural-deadline
    //      expiry (this watcher) OR by the player-floor in PerFrameTick
    //      calling beginScriptedDying directly. Either path runs the
    //      same transition function.
    //   2. Dying -> Felled when dying_until_wallclock passes. Routes
    //      through fireEnemyDeath, which plays the death clip and sets
    //      the archetype's felled_flag.
    // Player damage that drops HP to zero also routes through
    // fireEnemyDeath (Engaged -> Felled directly, skipping the pain
    // stage -- damage-death is its own visual via the hit-react path).
    const float now = selva::wallClock();
    for (std::size_t i = 0; i < actors().size(); ++i)
    {
        Actor& a = actors()[i];
        if (a.is_dead)
            continue;
        const bool in_scripted_death =
            (a.boss_state == BossState::Engaged || a.boss_state == BossState::Dying);
        if (!in_scripted_death)
            continue;
        if (a.scripted_death_drain_end_wallclock < 0.0f)
            continue;
        if (a.archetype != nullptr && a.archetype->scripted_death_drain_to_fraction > 0.0f)
        {
            // Drain runs across the full (Engaged + Dying) window.
            // During Engaged: curve approaches drain_to_fraction.
            // During Dying: continues from drain_to_fraction to 0 over
            // the pain-clip duration. Single continuous timeline.
            const float drain_start =
                a.scripted_death_at_wallclock - a.archetype->scripted_death_seconds;
            const float total_window = a.scripted_death_drain_end_wallclock - drain_start;
            const float elapsed = std::clamp(now - drain_start, 0.0f, total_window);
            const float t = total_window > 0.0f ? elapsed / total_window : 1.0f;
            const float engaged_fraction = a.archetype->scripted_death_seconds / total_window;
            const float drain_to = a.archetype->scripted_death_drain_to_fraction;
            float hp_fraction;
            if (t <= engaged_fraction)
            {
                const float t_engaged = engaged_fraction > 0.0f ? t / engaged_fraction : 1.0f;
                const float curve =
                    std::pow(t_engaged, a.archetype->scripted_death_drain_exponent);
                hp_fraction = 1.0f - (1.0f - drain_to) * curve;
            }
            else
            {
                const float dying_span = 1.0f - engaged_fraction;
                const float t_dying = dying_span > 0.0f ? (t - engaged_fraction) / dying_span : 1.0f;
                hp_fraction = drain_to * (1.0f - t_dying);
            }
            const int target_hp =
                static_cast<int>(std::round(static_cast<float>(a.hp.max) * hp_fraction));
            if (target_hp < a.hp.current)
                a.hp.current = std::max(0, target_hp);
        }
        if (a.boss_state == BossState::Engaged && now >= a.scripted_death_at_wallclock)
        {
            std::fprintf(stderr,
                         "[scripted-death] '%s' deadline reached at t=%.2f (hp was %d/%d)\n",
                         a.spawn_decl_id.c_str(), now, a.hp.current, a.hp.max);
            std::fflush(stderr);
            beginScriptedDying(a);
        }
    }
    for (std::size_t i = 0; i < actors().size(); ++i)
    {
        Actor& a = actors()[i];
        if (a.is_dead)
            continue;
        if (a.boss_state != BossState::Dying)
            continue;
        if (a.dying_until_wallclock < 0.0f || now < a.dying_until_wallclock)
            continue;
        std::fprintf(stderr,
                     "[scripted-death] '%s' pain stage ended at t=%.2f -> Felled\n",
                     a.spawn_decl_id.c_str(), now);
        std::fflush(stderr);
        const int enemy_idx = enemyIndex(a);
        fireEnemyDeath(a, enemy_idx);
    }
    // Active-boss disengage watcher. Walks the pool for any actor in
    // BossState::Engaged whose perception has dropped below Combat
    // (player out of leash for ai_combat_disengage_seconds). Transition
    // Engaged -> Disengaged via setBossState so mirrors + audio bed pop
    // + log all happen in the single funnel.
    //
    // Note: Felled is handled directly in fireEnemyDeath (also via
    // setBossState). This watcher only catches the non-death
    // "abandoned mid-fight" path.
    for (auto& a : actors())
    {
        if (a.boss_state != BossState::Engaged)
            continue;
        if (a.is_dead) // belt-and-suspenders; death should already have flipped to Felled
            continue;
        if (a.perception.awareness < Awareness::Combat)
        {
            setBossState(a, BossState::Disengaged);
        }
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

const Actor* enemyAt(int index)
{
    if (index < 0)
        return nullptr;
    int seen = 0;
    for (const auto& a : actors())
    {
        if (a.controller == Controller::Input)
            continue;
        if (seen == index)
            return &a;
        ++seen;
    }
    return nullptr;
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
    // Re-aggro: a boss that had Disengaged returns to Engaged on damage.
    // Dormant bosses (Pattern B pre-engage) do NOT auto-re-engage from
    // hits -- the engage_trigger is the authored entry point, and we
    // don't want a stray ranged hit to skip the held-pose theatrics.
    if (e.is_boss && e.boss_state == BossState::Disengaged)
        setBossState(e, BossState::Engaged);

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

    const HitReactPick pick = pickHitReactFamily(damage, e.yaw, world_normal);
    const ClipLookup hit_react = lookupArchetypeClip(e, pick.family);
    if (hit_react.clip == nullptr || !hit_react.clip->isLoaded())
        return;
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = hit_react.key;
    e.sampler.playOneShot(*hit_react.clip, pick.blend_in, pick.blend_out,
                          selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    e.last_hit_react_time = now;
    e.last_damage_time = now;
    selva::combat::combatLog("[hit-react] enemy[{}] dmg={} poise={:.1f}/{:.1f} -> clip={}", index,
                             damage, e.poise.current, e.poise.max, hit_react.key);
}

} // namespace selva::gameplay
