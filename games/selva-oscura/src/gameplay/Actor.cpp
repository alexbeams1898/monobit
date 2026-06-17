#include "gameplay/Actor.h"

#include "AppStateGlobal.h"
#include "Formulas.h"
#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "classmods/ClassModifiers.h"
#include "combat/ActorVolumes.h"
#include "combat/CombatLog.h"
#include "combat/HitVolumes.h"
#include "gameplay/EnemyArchetype.h"
#include "softcaps/SoftCaps.h"

#include <algorithm>
#include <cmath>

namespace selva::gameplay
{

int computeMaxHp(const Body& body, const Stats& stats, PlayerClass cls)
{
    const auto& f = selva::formulas::current();
    // Apply per-class soft-cap to END before it feeds the engine HP
    // formula. Penitent's END bends late (high cap, big returns deep
    // into investment); Heretic's END bends early. PlayerClass::None
    // (enemies) returns the raw stat. Per
    // [[project_class_stats_v2_locked_2026_06_14]].
    const float effective_end =
        selva::softcaps::apply(static_cast<float>(stats.end), cls, "hp_from_end");
    // Class HP offset is the flat contribution the class itself adds
    // ("the burden-bearing soul carries more vital substance"). Zero
    // for enemies + Unburdened; positive for the burdened classes.
    // Layers on top of the soft-cap-shaped END contribution.
    const int class_hp_offset = selva::classmods::offsetFor(cls, "hp_offset");
    return body.base_hp + class_hp_offset +
           static_cast<int>(std::floor(f.hp.base + effective_end * f.hp.scale));
}

float computeMaxStamina(const Body& body, const Stats& stats)
{
    const auto& f = selva::formulas::current();
    return static_cast<float>(body.base_stamina) + f.stamina.base +
           static_cast<float>(stats.end) * f.stamina.end_scale;
}

float computeMaxPoise(const Body& body, const Stats& stats)
{
    const auto& f = selva::formulas::current();
    const float end_bonus = static_cast<float>(stats.end) * f.poise.end_scale;
    const float str_bonus = static_cast<float>(stats.str) * f.poise.str_scale;
    return static_cast<float>(body.base_poise) + end_bonus + str_bonus;
}

void applyFormDefaults(Body& body, Stats& stats, Form form)
{
    // Baseline body/stats per cosmological form. Numbers are starting
    // points -- archetype overrides layer on top. Form drives the
    // SHAPE (a wolf has more raw HP than a shade because animal-form
    // has biological mass; a Guide has less HP than a shade because
    // unjudged-soul has no substrate); per-instance authoring tunes
    // within the shape. Per [[soul-animal-form-combat-doctrine]] +
    // bestiary.md.
    // Collider defaults: every humanoid form uses the standard Vagrant
    // capsule. Animal approximates a quadruped via a vertical capsule
    // sized to the wolf's standing bounds (Jolt only supports vertical
    // character capsules today; we approximate AlongYaw shapes as
    // vertical for the physics body). Per-archetype overrides can
    // tune further. Per universal-actor-physics doctrine.
    auto applyHumanoidCollider = [&]()
    {
        body.collider_radius = 0.35f;
        body.collider_height = 1.8f;
        body.collider_axis = CapsuleAxis::Vertical;
    };
    switch (form)
    {
    case Form::UnjudgedSoul:
        body.base_hp = 60;
        body.base_poise = 12;
        body.base_stamina = 60;
        body.base_defense = 0;
        stats = {1, 1, 1, 1};
        applyHumanoidCollider();
        break;
    case Form::DamnedSoul:
        // Matches the legacy shade defaults so existing JSON behavior
        // is preserved when this function is called on shades.
        body.base_hp = 50;
        body.base_poise = 30;
        body.base_stamina = 80;
        body.base_defense = 0;
        stats = {1, 1, 1, 1};
        applyHumanoidCollider();
        break;
    case Form::Animal:
        body.base_hp = 200;
        body.base_poise = 50;
        body.base_stamina = 150;
        body.base_defense = 2;
        stats = {3, 2, 3, 1};
        body.collider_radius = 0.55f;
        body.collider_height = 1.2f;
        body.collider_axis = CapsuleAxis::AlongYaw;
        body.collider_length = 1.6f;
        break;
    case Form::HellMachinery:
        // Reserved -- keepers not shipped yet. Legendary-tier numbers
        // so future tuning starts from a high baseline.
        body.base_hp = 1500;
        body.base_poise = 200;
        body.base_stamina = 300;
        body.base_defense = 8;
        stats = {5, 4, 5, 1};
        applyHumanoidCollider();
        body.collider_height = 2.4f; // keeper-tier taller
        break;
    case Form::Divine:
        // Reserved -- Beatrice not shipped yet. Boss-class numbers
        // but the actual fight is "fragmentary, rabid" per canon, so
        // these are placeholders; tune when implementing.
        body.base_hp = 800;
        body.base_poise = 150;
        body.base_stamina = 200;
        body.base_defense = 5;
        stats = {4, 4, 4, 4};
        applyHumanoidCollider();
        break;
    }
}

void initActorPools(Health& hp, Stamina& stamina, Poise& poise, const Body& body,
                    const Stats& stats, PlayerClass cls)
{
    hp.max = computeMaxHp(body, stats, cls);
    hp.current = hp.max;
    stamina.max = computeMaxStamina(body, stats);
    stamina.current = stamina.max;
    poise.max = computeMaxPoise(body, stats);
    poise.current = poise.max;
    poise.last_damage_time = -1.0f;
}

void tickActorStamina(Actor& a, float dt)
{
    if (a.is_dead)
        return;
    const auto& fc = selva::formulas::current();

    // Sprint lockout: ignore sprint input while locked. Lock releases below.
    if (a.stamina.sprint_locked && a.loco_tier == LocoTier::Sprint)
        a.loco_tier = LocoTier::Jog;

    if (a.loco_tier == LocoTier::Sprint && a.stamina.current > 0.0f)
    {
        a.stamina.current = std::max(0.0f, a.stamina.current - fc.stamina.sprint_effort * dt);
        a.stamina.recovery_timer = fc.stamina.recovery_delay;
        if (a.stamina.current <= 0.0f)
        {
            a.stamina.sprint_locked = true;
            a.loco_tier = LocoTier::Jog;
        }
    }
    else if (a.stamina.recovery_timer > 0.0f)
    {
        a.stamina.recovery_timer = std::max(0.0f, a.stamina.recovery_timer - dt);
    }
    else if (a.stamina.current < a.stamina.max)
    {
        a.stamina.current =
            std::min(a.stamina.max, a.stamina.current + fc.stamina.recovery_rate * dt);
    }

    if (a.stamina.sprint_locked && a.stamina.current >= a.stamina.max)
        a.stamina.sprint_locked = false;
}

bool canSpendStamina(const Actor& a, float cost)
{
    if (cost <= 0.0f)
        return true;
    return a.stamina.current >= cost;
}

void spendStamina(Actor& a, float cost)
{
    if (cost <= 0.0f)
        return;
    const auto& fc = selva::formulas::current();
    a.stamina.current = std::max(0.0f, a.stamina.current - cost);
    a.stamina.recovery_timer = fc.stamina.recovery_delay;
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

int computeAttackDamage(const Stats& attacker, const DamageInputs& inputs, int identity_value,
                        float identity_scaling)
{
    const float str_b = std::floor(static_cast<float>(attacker.str) * inputs.str_scaling);
    const float dex_b = std::floor(static_cast<float>(attacker.dex) * inputs.dex_scaling);
    const float end_b = std::floor(static_cast<float>(attacker.end) * inputs.end_scaling);
    const float lck_b = std::floor(static_cast<float>(attacker.lck) * inputs.lck_scaling);
    // Mind axes scale from the Mind sub-stats stored on the actor's
    // Stats per [[cognition-system-v1]]. v1 actor::Stats does not yet
    // carry Mind axes; the multiply collapses to zero until those
    // ship. The formula bakes them in now so weapon JSON authoring
    // (per_scaling / cog_scaling / int_scaling) is forward-compatible.
    const float per_b = 0.0f; // attacker.per * inputs.per_scaling -- pending Stats expansion
    const float cog_b = 0.0f; // attacker.cog * inputs.cog_scaling -- pending Stats expansion
    const float int_b = 0.0f; // attacker.intl * inputs.int_scaling -- pending Stats expansion
    const float identity_b = std::floor(static_cast<float>(identity_value) * identity_scaling);
    const float total =
        inputs.base_damage + str_b + dex_b + end_b + lck_b + per_b + cog_b + int_b + identity_b;
    if (total <= 0.0f)
        return 0;
    return static_cast<int>(total);
}

Actor* resolveLockTarget(const Actor& actor)
{
    const int idx = actor.lock_target_idx;
    if (idx < 0)
        return nullptr;
    auto& pool = actors();
    if (idx >= static_cast<int>(pool.size()))
        return nullptr;
    return &pool[idx];
}

const std::vector<selva::anim::LockOnPointDecl>& actorLockOnPoints(const Actor& actor)
{
    if (actor.archetype != nullptr && !actor.archetype->lockon_points.empty())
        return actor.archetype->lockon_points;
    return selva::anim::jointMapByKey(actor.skeleton_id).default_lockon_points;
}

int defaultLockOnPointIndex(const Actor& actor)
{
    const auto& pts = actorLockOnPoints(actor);
    if (pts.empty())
        return -1;
    for (std::size_t i = 0; i < pts.size(); ++i)
        if (pts[i].is_default)
            return static_cast<int>(i);
    return 0;
}

const char* directionalLocoClip(const glm::vec3& fwd, const glm::vec3& right,
                                const glm::vec3& intent, LocoTier tier, bool is_armed)
{
    if (glm::length(intent) <= 0.0001f)
        return nullptr;
    const float fwd_dot = glm::dot(intent, fwd);
    const float right_dot = glm::dot(intent, right);
    // Any nonzero lateral input wins → strafe. Forward/back only on
    // essentially pure W/S input.
    const bool axis_is_forward = std::abs(right_dot) < 1e-3f;
    // Sprint only has a forward clip; backward/strafe demote to jog.
    if (axis_is_forward && fwd_dot >= 0.0f && tier == LocoTier::Sprint)
        return is_armed ? "sword_and_shield_run_grip" : "sprinting";
    const LocoTier ground_tier = (tier == LocoTier::Sprint) ? LocoTier::Jog : tier;
    const bool walk = (ground_tier == LocoTier::Walk);
    if (is_armed)
    {
        // Pro Sword and Shield Pack ships walk forward/back + strafe
        // L/R + run forward. Jog back / jog strafe / sprint side-and-
        // back have no armed clips -- demote to the armed walk variant
        // for those directions.
        if (axis_is_forward)
        {
            if (fwd_dot >= 0.0f)
                return walk ? "sword_and_shield_walk_grip" : "sword_and_shield_run_grip";
            return "sword_and_shield_walk_2_grip";
        }
        if (right_dot >= 0.0f)
            return "sword_and_shield_strafe_grip";
        return "sword_and_shield_strafe_2_grip";
    }
    if (axis_is_forward)
    {
        if (fwd_dot >= 0.0f)
            return walk ? "walking" : "jogging";
        return walk ? "walking_backward" : "jogging_backward";
    }
    if (right_dot >= 0.0f)
        return walk ? "strafe_walking_right" : "strafe_jogging_right";
    return walk ? "strafe_walking_left" : "strafe_jogging_left";
}

glm::vec2 hipDeltaVelocityContribution(const glm::vec3& hip_local, float yaw, float dt,
                                       float hip_delta_scale, float body_scale)
{
    if (std::abs(hip_local.x) <= 1e-6f && std::abs(hip_local.z) <= 1e-6f)
        return glm::vec2(0.0f);
    if (dt <= 0.0001f)
        return glm::vec2(0.0f);
    const float sy = std::sin(yaw);
    const float cy = std::cos(yaw);
    const glm::vec3 hip_world(-cy * hip_local.x - sy * hip_local.z, 0.0f,
                              sy * hip_local.x - cy * hip_local.z);
    // body_scale: the visible mesh is rendered at body_scale of bind-
    // pose size by buildActorModelMatrix. The clip's authored hip
    // delta is in BIND-POSE meters (the clip was authored for the
    // unit-scale rig). A 0.85-scale shade's feet move 0.85 m for
    // every 1 m the clip says; if we apply the unscaled delta to
    // world pos, the world moves faster than the feet visibly do and
    // the actor slides. Per
    // [[feedback_hip_delta_two_sides]] + [[feedback_pose_sampler_key_propagation]]
    // -- the two-sides contract here is "world motion must equal
    // visible mesh foot motion."
    const float scale = hip_delta_scale * body_scale;
    return glm::vec2(hip_world.x * scale / dt, hip_world.z * scale / dt);
}

void applyActorClipHipDelta(Actor& actor, float dt, float hip_delta_scale)
{
    // Contract from feedback_hip_delta_two_sides.md: PoseSampler
    // extracts the clip's authored hip-XZ and zeros it in the local
    // pose. Gameplay must apply that consumed delta back to world
    // motion or the actor's feet treadmill. Routed through the unified
    // physics pipeline (universal-actor-physics refactor): the hip
    // delta is converted to a velocity contribution that the next
    // physics step integrates. Direct pos writes are gone.
    //
    // Add hip-derived velocity ON TOP of whatever the locomotion tick
    // wrote. For root-motion clips the locomotion tick zeroes
    // velocity_xz first, so this becomes the sole contribution; for
    // mixed cases (a one-shot with authored hip motion firing while
    // a velocity gait was active) the two compose.
    const glm::vec3 hip_local = actor.sampler.consumedHipDelta();
    actor.velocity_xz += hipDeltaVelocityContribution(hip_local, actor.yaw, dt, hip_delta_scale,
                                                      actor.appearance.body_scale);
}

bool actorCanLandHits(const Actor& a)
{
    if (a.is_dead || a.is_knocked_down)
        return false;
    switch (a.boss_state)
    {
    case BossState::Dying:
    case BossState::Felled:
    case BossState::Disengaged:
        return false;
    case BossState::Dormant:
    case BossState::Engaged:
        return true;
    }
    return true;
}

void updateActiveAttackHitbox(Actor& actor)
{
    if (actor.active_attack_hitbox_id == 0 || actor.active_attack_joint_idx < 0)
        return;
    selva::combat::Hitbox* hb = selva::combat::findHitbox(actor.active_attack_hitbox_id);
    if (hb == nullptr)
    {
        // Lifetime elapsed — tickHitboxes erased it. Clear our handle.
        actor.active_attack_hitbox_id = 0;
        actor.active_attack_joint_idx = -1;
        actor.active_attack_tip_offset_z = 0.0f;
        return;
    }
    // Re-anchor the capsule to the joint's current world pose so the
    // swept hit-detection sweeps from prev_shape (last frame) to
    // shape (this frame), covering the swing arc. Without this the
    // capsule sits frozen at the spawn-frame pose (a windup with the
    // hand at the hip), and the swing visually contacts the player
    // while the volume sits behind the attacker — visible as "AI
    // punches into the player but no damage."
    const float foot_offset_y = actorFootOffsetY(actor);
    const glm::mat4 model_mat = selva::combat::buildActorModelMatrix(
        actor.pos, actor.yaw, foot_offset_y, actor.appearance.body_scale);
    const glm::vec3 anchor_world = glm::vec3(
        model_mat * glm::vec4(actor.sampler.jointWorldPos(actor.active_attack_joint_idx), 1.0f));
    const glm::vec3 tip_world =
        (actor.active_attack_tip_offset_z != 0.0f)
            ? anchor_world +
                  glm::vec3(model_mat *
                            glm::vec4(0.0f, 0.0f, actor.active_attack_tip_offset_z, 0.0f))
            : anchor_world;
    // Snapshot the PRE-update shape into prev_shape before we
    // overwrite it. detectHits later this frame uses (prev, curr) to
    // build the swept capsule covering the motion between the last
    // frame's pose and this one. Doing the snapshot here, rather
    // than in tickHitboxes, makes prev always be "the shape from one
    // frame ago at this exact same spawn-then-update lifecycle
    // moment" regardless of where in the per-frame order tickHitboxes
    // lands. Before this fix, tickHitboxes ran AFTER the shape
    // update but BEFORE detectHits, copying curr -> prev and
    // degenerating the swept capsule to a point -- no sweep, no hit.
    // First frame after spawn: prev == shape == anchor_world (set by
    // spawnHitbox), so the swept capsule collapses to the spawn-frame
    // sphere. Subsequent frames see real motion.
    hb->prev_shape = hb->shape;
    hb->has_prev = true;
    hb->shape.p0 = anchor_world;
    hb->shape.p1 = tip_world;
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

Actor* actorByDeclId(const std::string& spawn_decl_id)
{
    if (spawn_decl_id.empty())
        return nullptr;
    for (auto& a : sActors)
        if (a.spawn_decl_id == spawn_decl_id)
            return &a;
    return nullptr;
}

void initActorPool()
{
    sActors.clear();
    Actor pc;
    pc.controller = Controller::Input;
    pc.faction = Faction::Player;
    // The Vagrant is an unjudged soul -- refused Hell's measurement,
    // received by the selva oscura. Form drives base stat-spread
    // (low HP, low poise, fragile vessel) before class-pick layers on
    // top. Per [[soul-animal-form-combat-doctrine]] +
    // story.md *The Guide / Identity* (the Vagrant is the second
    // unjudged-soul, after the Guide).
    pc.form = Form::UnjudgedSoul;
    pc.skeleton_id = "player";
    // Player hurtbox layout. Authored in
    // config/skeletons/player_hurtboxes.json -- the data form of what
    // ActorVolumes.cpp::appendActorHurtboxes used to hardcode.
    pc.body.hurtbox_decls =
        selva::combat::loadHurtboxDecls("config/skeletons/player_hurtboxes.json");
    pc.death_clip_name = "second_death";
    // The PC's death audio is a layered composition:
    //   * death_sfx_name (dark bed) — plays at clip start, dread
    //     under the fall.
    //   * death_peak_sfx_names (verdict jump-scare) — multiple SFX
    //     scheduled so each one's peak lands at the moment the
    //     second-death card snaps in. The align target matches
    //     kPlayerSecondDeathClipHoldSeconds in PerFrameTick /
    //     ActorHud (both consume the same 3.5s).
    pc.death_sfx_name = "dark_sound";
    pc.death_peak_sfx_names = {"synth_echo", "soul_steal"};
    pc.death_peak_align_seconds = 3.5f;
    // Apply form-defaults to Body + Stats before pool init. The class-
    // pick system will layer custom stat spreads on top; the baseline
    // applied here is the unjudged-soul default (60 HP / 12 poise).
    applyFormDefaults(pc.body, pc.stats, pc.form);
    // The player's class drives the HP soft-cap; pre-Beat-4 (None)
    // applies no cap. PerFrameTick re-runs initActorPools when class
    // changes (Signing fire / Erasure) so cap shifts apply
    // immediately.
    const auto* profile = selva::activePlayerProfile();
    const selva::PlayerClass cls =
        (profile != nullptr) ? profile->player_class : selva::PlayerClass::None;
    initActorPools(pc.hp, pc.stamina, pc.poise, pc.body, pc.stats, cls);
    // Visual appearance load. Empty appearance_path => default
    // (body_scale = 1.0); a valid path resolves through
    // loadAppearance which tolerates missing/malformed files by
    // returning defaults. Note: at boot, profile is nullptr (no
    // character selected yet); resetPlayerActorForProfile re-loads
    // appearance per character on Playing-enter, which is when the
    // player actually sees their body.
    if (profile != nullptr)
        pc.appearance = loadAppearance(profile->appearance_path);
    // initActorPool is one-time, asset-binding only. Position, hp-fill,
    // and any per-character state are NOT set here - those land via
    // hardResetWorldForCharacter(profile) on Playing-enter. This
    // separation is what lets New Game / Load Game both produce a
    // clean spawn instead of inheriting the previous run's state.
    //
    // pos defaults to (0, 0, 0) until hardResetWorldForCharacter
    // sets it; that's fine because nothing reads sPlayer.pos before
    // we enter Playing.
    pc.spawn_pos = glm::vec3(0.0f);
    // Player's sampler is bound to the shared skeleton + mesh at
    // first sampler.update() — same as any other actor. Pre-warm
    // it so the bone palette is valid before render.
    pc.sampler = selva::anim::createPoseSampler(selva::anim::skeleton(), selva::anim::playerMesh());
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
        applyActorClipHipDelta(a, dt);
    }
}

float actorFootOffsetY(const Actor& a)
{
    const std::string key = a.skeleton_id.empty() ? std::string("player") : a.skeleton_id;
    return selva::anim::meshByKey(key).foot_offset_y;
}

} // namespace selva::gameplay
