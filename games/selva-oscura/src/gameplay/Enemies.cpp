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
#include "debug/Flags.h"
#include "dialog/DialogSystem.h"
#include "gameplay/AiTick.h"
#include "gameplay/BehaviorTree.h"
#include "gameplay/BossRewards.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/Perception.h"
#include "gameplay/Sangue.h"
#include "gameplay/SanguePulse.h"
#include "hazard/HazardZones.h"
#include "insight/Insight.h"
#include "interact/Interaction.h"
#include "items/ItemRegistry.h"
#include "lang/Language.h"
#include "loot/Pickups.h"
#include "ops/InventoryOps.h"
#include "text/Examine.h"
#include "text/TextPresentation.h"
#include "world/Collision.h"
#include "world/PhysicsRegion.h"
#include "world/RegionBootstrap.h"
#include "world/Terrain.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <random>

namespace selva::gameplay
{

namespace
{

// Severity tier + cooldown + respawn knobs live in Tunables so
// they're hot-reloadable from the F1 panel and serialize with the
// rest of the game's feel parameters. Read fresh each call.

constexpr float kEnemyWalkSpeedFloor = 0.15f; // m/s; above = walk, below = idle
constexpr float kEnemyRunSpeedFloor = 3.0f;   // m/s; above = Run family, below = Walk family

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
// Data table indexed by ClipFamily. Table replaces a long switch
// statement (lizard CCN budget). New family = add struct field +
// row. Indices match ClipFamily enum order; static_assert at the
// bottom catches misalignment.
struct ClipFamilyRow
{
    ClipFamily fam;
    const char* default_key;
};
constexpr ClipFamilyRow kClipFamilyTable[] = {
    {ClipFamily::PeacefulIdle, "standard_idle"},
    {ClipFamily::CombatIdle, "unarmed_combat_idle"},
    {ClipFamily::Walk, "walking"},
    {ClipFamily::Run, "jogging"},
    {ClipFamily::WalkBack, "walking_backward"},
    {ClipFamily::StrafeLeft, "strafe_walking_left"},
    {ClipFamily::StrafeRight, "strafe_walking_right"},
    {ClipFamily::Death, "death"},
    {ClipFamily::Knockdown, "stunned"},
    {ClipFamily::FlinchFront, "flinch_front"},
    {ClipFamily::FlinchBack, "flinch_back"},
    {ClipFamily::FlinchLeft, "flinch_left"},
    {ClipFamily::FlinchRight, "flinch_right"},
    {ClipFamily::HitReactMedium, "hit_react_medium"},
    {ClipFamily::HitReactHeavy, "hit_react_heavy"},
};

const char* clipFamilyDefaultKey(ClipFamily fam)
{
    for (const auto& row : kClipFamilyTable)
        if (row.fam == fam)
            return row.default_key;
    return "";
}

// Table of pointer-to-member-string for the archetype-override lookup.
// Same row count as kClipFamilyTable; matched by ClipFamily key.
struct ClipFamilyOverrideRow
{
    ClipFamily fam;
    std::string EnemyArchetype::*member;
};
const ClipFamilyOverrideRow kClipFamilyOverrideTable[] = {
    {ClipFamily::PeacefulIdle, &EnemyArchetype::idle_clip},
    {ClipFamily::CombatIdle, &EnemyArchetype::combat_idle_clip},
    {ClipFamily::Walk, &EnemyArchetype::walk_clip},
    {ClipFamily::Run, &EnemyArchetype::run_clip},
    {ClipFamily::WalkBack, &EnemyArchetype::walk_back_clip},
    {ClipFamily::StrafeLeft, &EnemyArchetype::strafe_left_clip},
    {ClipFamily::StrafeRight, &EnemyArchetype::strafe_right_clip},
    {ClipFamily::Death, &EnemyArchetype::death_clip},
    {ClipFamily::Knockdown, &EnemyArchetype::knockdown_clip},
    {ClipFamily::FlinchFront, &EnemyArchetype::flinch_front_clip},
    {ClipFamily::FlinchBack, &EnemyArchetype::flinch_back_clip},
    {ClipFamily::FlinchLeft, &EnemyArchetype::flinch_left_clip},
    {ClipFamily::FlinchRight, &EnemyArchetype::flinch_right_clip},
    {ClipFamily::HitReactMedium, &EnemyArchetype::hit_react_medium_clip},
    {ClipFamily::HitReactHeavy, &EnemyArchetype::hit_react_heavy_clip},
};

const std::string& clipFamilyArchetypeOverride(const EnemyArchetype& arch, ClipFamily fam)
{
    static const std::string kEmpty;
    for (const auto& row : kClipFamilyOverrideTable)
        if (row.fam == fam)
            return arch.*(row.member);
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

} // namespace
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
void initActorPoolsForArchetype(Actor& a)
{
    // Enemies never get the player's soft-cap; their HP is tuned via
    // archetype overrides (max_hp_override below) on top of the raw
    // engine formula. Pass PlayerClass::None to skip the per-class
    // cap entirely.
    initActorPools(a.hp, a.stamina, a.poise, a.body, a.stats, selva::PlayerClass::None);
    if (a.archetype != nullptr)
    {
        if (a.archetype->max_hp_override > 0)
        {
            a.hp.max = a.archetype->max_hp_override;
            a.hp.current = a.hp.max;
        }
        if (a.archetype->max_poise_override > 0.0f)
        {
            a.poise.max = a.archetype->max_poise_override;
            a.poise.current = a.archetype->max_poise_override;
        }
    }
}

// Resolve the actor's hurtbox layout from its current archetype.
// Centralized so both spawn (spawnEnemyFromDecl) AND mid-life
// archetype swaps (applyArchetypeSwap) use the same contract. Three
// branches:
//   * disable_hurtboxes=true -> explicitly no hurtboxes; this being
//     cannot be hit (fresh larvae per the "too coherent for second-
//     death" cosmology, future intact NPCs).
//   * Archetype's own list non-empty -> use it.
//   * Archetype list empty -> inherit player's hurtboxes (humanoid
//     shades sharing the humanoid skeleton).
void applyArchetypeHurtboxes(Actor& a)
{
    if (a.archetype != nullptr && a.archetype->disable_hurtboxes)
        a.body.hurtbox_decls.clear();
    else if (a.archetype != nullptr && !a.archetype->hurtbox_decls.empty())
        a.body.hurtbox_decls = a.archetype->hurtbox_decls;
    else
        a.body.hurtbox_decls = selva::gameplay::player().body.hurtbox_decls;
}

// Resolve the actor's interactable registration from its current
// archetype. Idempotent: tears down any existing interactable
// (so a swap from examinable -> non-examinable clears the prompt)
// and registers a fresh one if the archetype declares interaction.
// Two paths:
//   * is_npc=true (Guide, future companions) -> Talk-kind, opens
//     dialog tree.
//   * Non-empty examine_text (fresh larvae, ambient observables)
//     -> Examine-kind, opens one-line examine box.
// is_npc takes precedence per the "named NPCs have dialog,
// anonymous beings have examine" doctrine.
// Resolve the player-visible label for an NPC's Talk interactable.
// Preference order: display_name_key (lang) > display_name (literal) >
// spawn_decl_id (designer fallback).
std::string resolveNpcTalkLabel(const Actor& a, const std::string& sdid)
{
    if (!a.archetype->display_name_key.empty())
        return selva::lang::resolve(a.archetype->display_name_key);
    if (!a.archetype->display_name.empty())
        return a.archetype->display_name;
    return sdid;
}

// Resolve an actor's chest world position. Used by Talk + Examine
// decl `position` closures so the focus highlight ring renders at
// the chest rather than the feet -- same convention as Souls /
// Elden Ring "talk-to / examine" anchor visuals. Resolves to the
// first lockon-point joint when the skeleton authors one (default
// for humanoid is "mixamorig:Spine2" = chest); falls back to
// (pos.y + collider_height * 0.7) so actors without a lockon-point
// rig still get a roughly-correct chest height. Costs one joint
// lookup per frame the player is in range -- cheap.
glm::vec3 actorChestWorldPos(const Actor& act)
{
    const auto& points = selva::gameplay::actorLockOnPoints(act);
    if (!points.empty())
    {
        const int joint_idx = act.sampler.findJoint(points[0].joint.c_str());
        if (joint_idx >= 0)
        {
            const float foot_offset = selva::gameplay::actorFootOffsetY(act);
            const glm::mat4 model = selva::combat::buildActorModelMatrix(
                act.pos, act.yaw, foot_offset, act.appearance.body_scale);
            const glm::vec3 local = act.sampler.jointWorldPos(joint_idx);
            return glm::vec3(model * glm::vec4(local, 1.0f));
        }
    }
    return glm::vec3(act.pos.x, act.pos.y + act.body.collider_height * 0.7f, act.pos.z);
}

// Build the NPC Talk Decl. Talk_requires_flag gates availability
// (empty = always talkable). Snaps the NPC's facing toward the player
// at dialog-open.
selva::interact::Decl buildNpcTalkDecl(const Actor& a, const std::string& sdid)
{
    const std::string label = resolveNpcTalkLabel(a, sdid);
    const std::string required_flag = a.archetype->talk_requires_flag;
    selva::interact::Decl idecl;
    idecl.kind = selva::interact::Kind::Talk;
    idecl.position = [sdid]()
    {
        const Actor* act = actorByDeclId(sdid);
        return act != nullptr ? actorChestWorldPos(*act) : glm::vec3(0.0f);
    };
    constexpr float kDefaultTalkRangeMeters = 2.5f;
    idecl.range_meters = (a.archetype->interact_range_meters > 0.0f)
                             ? a.archetype->interact_range_meters
                             : kDefaultTalkRangeMeters;
    idecl.label = label;
    idecl.on_interact = [sdid]()
    {
        Actor* act = actorByDeclId(sdid);
        if (act != nullptr)
            act->turn_intent_yaw = yawFacing(act->pos, player().pos);
        selva::dialog::begin(sdid);
    };
    idecl.available = [required_flag]()
    {
        if (selva::text::active())
            return false;
        return required_flag.empty() || selva::hasFlag(required_flag);
    };
    return idecl;
}

// Resolve the player-visible label for an Examine interactable.
// Preference order: examine_label_key > display_name_key > display_name > empty.
std::string resolveExamineLabel(const Actor& a)
{
    if (!a.archetype->examine_label_key.empty())
        return selva::lang::resolve(a.archetype->examine_label_key);
    if (!a.archetype->display_name_key.empty())
        return selva::lang::resolve(a.archetype->display_name_key);
    return a.archetype->display_name;
}

// Build the Examine Decl for a non-NPC actor. Text resolution: lang
// key wins, literal fallback. Examine fires an insight notify via the
// actor's archetype id.
selva::interact::Decl buildExamineDecl(const Actor& a, const std::string& sdid)
{
    selva::interact::Decl idecl;
    idecl.kind = selva::interact::Kind::Examine;
    idecl.position = [sdid]()
    {
        const Actor* act = actorByDeclId(sdid);
        return act != nullptr ? actorChestWorldPos(*act) : glm::vec3(0.0f);
    };
    constexpr float kDefaultExamineRangeMeters = 2.0f;
    idecl.range_meters = (a.archetype->interact_range_meters > 0.0f)
                             ? a.archetype->interact_range_meters
                             : kDefaultExamineRangeMeters;
    idecl.label = resolveExamineLabel(a);
    idecl.on_interact = [sdid]()
    {
        const Actor* act = actorByDeclId(sdid);
        if (act == nullptr || act->archetype == nullptr)
            return;
        const std::string text = act->archetype->examine_text_key.empty()
                                     ? act->archetype->examine_text
                                     : selva::lang::resolve(act->archetype->examine_text_key);
        selva::text::beginExamine(text);
        selva::insight::notifyExamined(act->archetype->id);
    };
    // Examine is a non-combat verb -- it only makes sense when the
    // actor is alive AND in its resting pose (perception.awareness ==
    // Unaware). The moment perception escalates (Suspicious / Alerted
    // / Combat), the actor is in motion / posing for combat and an
    // Examine prompt would visually fight the chase animation. For
    // the soul-larva feeder specifically this is what makes the
    // feeding-cycle examine fire ONLY while the feeder is crouched
    // and biting (its Unaware resting pose); a feeder that has stood
    // up and is chasing the player drops out of the Examine list
    // until it disengages.
    idecl.available = [sdid]()
    {
        if (selva::text::active())
            return false;
        const Actor* act = actorByDeclId(sdid);
        if (act == nullptr || act->is_dead)
            return false;
        return act->perception.awareness == selva::gameplay::Awareness::Unaware;
    };
    return idecl;
}

void applyArchetypeInteractable(Actor& a)
{
    if (a.interactable_id != 0)
    {
        selva::interact::unregisterInteractable(a.interactable_id);
        a.interactable_id = 0;
    }
    if (a.archetype == nullptr)
        return;
    const std::string sdid = a.spawn_decl_id;
    if (a.is_npc)
    {
        a.interactable_id = selva::interact::registerInteractable(buildNpcTalkDecl(a, sdid));
        return;
    }
    if (!a.archetype->examine_text.empty() || !a.archetype->examine_text_key.empty())
    {
        a.interactable_id = selva::interact::registerInteractable(buildExamineDecl(a, sdid));
    }
}

// THE archetype-to-actor funnel. Single source of truth for "given
// this actor and a target archetype, derive every archetype-driven
// field." Called from spawnEnemyFromDecl (initial setup) AND
// applyArchetypeSwap (mid-life transition). Adding a new archetype-
// derived field means updating ONE site -- not two.
//
// Does NOT touch:
//   * Identity: spawn_decl_id, spawn_pos, controller, rng (set once)
//   * Sampler / skeleton binding (bound at spawn; cross-skeleton
//     swaps would require sampler recreation -- not v1)
//   * Physics body handle (created at spawn; destroyed at death).
//     applyArchetypeSwap rebuilds the Jolt capsule itself when
//     collider dims change -- this funnel writes body.collider_*
//     fields but doesn't reach into Jolt.
//   * Boss state machine (setBossState orchestrates transitions)
//   * Animation transitions (e.g. aggro_clip play on swap, idle
//     prime on spawn) -- those are CONTEXTUAL events the caller
//     owns, not derived state.
//
// This is the diamond-foundation funnel. Per
// [[feedback_dual_source_of_truth_is_the_bug]].
void applyArchetypeToActor(Actor& a, const EnemyArchetype& target)
{
    a.archetype = &target;
    a.faction = target.faction;
    a.form = target.form;
    a.is_boss = target.is_boss;
    a.is_npc = target.is_npc;
    // Per-form body/stat defaults derive from form, which we just set.
    applyFormDefaults(a.body, a.stats, a.form);
    initActorPoolsForArchetype(a);
    applyArchetypeHurtboxes(a);
    applyArchetypeInteractable(a);
    // Visual appearance: every actor reads body_scale through its
    // appearance field. Empty appearance_path returns the default
    // (body_scale = 1.0) so legacy archetypes that don't author an
    // appearance stay identical to today.
    a.appearance = loadAppearance(target.appearance_path);
}

namespace
{
// Resolve pos.y for the "auto_terrain" sentinel + mirror per-flag spawn
// overrides. Identity-only setup; no archetype lookup yet.
void initSpawnIdentity(Actor& e, const std::string& region_id, const EnemySpawnDecl& decl,
                       const EnemyArchetype* /*archetype_ptr*/)
{
    e.controller = Controller::AI_Stationary;
    e.pos = decl.pos;
    if (decl.pos_y_auto_terrain)
        e.pos.y = selva::world::groundHeight(decl.pos.x, decl.pos.z,
                                             -std::numeric_limits<float>::infinity());
    e.yaw = decl.yaw;
    // Post-flag pos overrides: if any flag-conditioned override is
    // active for this actor at spawn time (e.g. "if signing committed,
    // Guide stands at the chapel door"), apply it now. Walk in JSON
    // order; later entries win on tie so authors can layer conditions.
    // This used to run at reset time too, but reset now rebuilds the
    // pool from spawn (per [[feedback_spawn_systems_need_idempotency]]),
    // so the same spawn-path logic covers both first-boot and reload.
    for (const auto& fp : decl.post_flag_positions)
    {
        if (!selva::hasFlag(fp.flag))
            continue;
        glm::vec3 override_pos = fp.pos;
        if (fp.pos_y_auto_terrain)
            override_pos.y =
                selva::world::groundHeight(override_pos.x, override_pos.z, override_pos.y + 100.0f);
        e.pos = override_pos;
        e.yaw = fp.yaw;
    }
    e.spawn_pos = e.pos;
    e.spawn_yaw = e.yaw;
    e.spawn_id = region_id + ":" + decl.id;
    e.spawn_region_id = region_id;
    e.permanent_on_death = decl.permanent_on_death;
    e.spawn_decl_id = decl.id;
    // post_flag_positions kept on the actor for diagnostics/future
    // mid-life re-evaluation. Today only spawn-time uses them.
    e.post_flag_positions.clear();
    e.post_flag_positions.reserve(decl.post_flag_positions.size());
    for (const auto& fp : decl.post_flag_positions)
    {
        Actor::FlagPosition mirror;
        mirror.flag = fp.flag;
        mirror.pos = fp.pos;
        mirror.pos_y_auto_terrain = fp.pos_y_auto_terrain;
        mirror.yaw = fp.yaw;
        e.post_flag_positions.push_back(std::move(mirror));
    }
}

// Bind sampler to the archetype's skeleton + run the diamond funnel.
// archetype_ptr is the early lookup result from spawnEnemyFromDecl
// (passed in so we don't repeat the registry lookup).
void bindSpawnArchetype(Actor& e, const EnemySpawnDecl& decl, const EnemyArchetype* archetype_ptr)
{
    if (!decl.archetype.empty())
    {
        e.archetype = (archetype_ptr != nullptr) ? archetype_ptr : archetypes().get(decl.archetype);
        if (e.archetype == nullptr)
            selva::combat::combatLog("[spawn] archetype '{}' not found in registry (id='{}')",
                                     decl.archetype, e.spawn_id);
    }
    e.skeleton_id = (e.archetype != nullptr) ? e.archetype->skeleton_id : std::string("player");
    e.sampler = selva::anim::createPoseSampler(selva::anim::skeletonByKey(e.skeleton_id),
                                               selva::anim::meshByKey(e.skeleton_id),
                                               selva::anim::jointMapByKey(e.skeleton_id));
    std::random_device rd;
    e.rng.seed(rd() ^ static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.x * 1000.0f)) ^
               static_cast<std::uint32_t>(static_cast<std::int64_t>(decl.pos.z * 1000.0f)));
    if (e.archetype != nullptr)
    {
        applyArchetypeToActor(e, *e.archetype);
        if (selva::debug::flags().enemy_lifecycle)
        {
            std::fprintf(stderr,
                         "[spawn] '%s' archetype='%s' form=%s hp.max=%d poise.max=%.1f "
                         "(overrides: hp=%d poise=%.1f)\n",
                         e.spawn_id.c_str(), decl.archetype.c_str(), formName(e.form), e.hp.max,
                         e.poise.max, e.archetype->max_hp_override,
                         e.archetype->max_poise_override);
            std::fflush(stderr);
        }
    }
    else
    {
        applyFormDefaults(e.body, e.stats, e.form);
        initActorPoolsForArchetype(e);
        e.faction = Faction::Hostile;
    }
}

// Decl shape (intermediates + final destination) -> actor runtime shape
// (current + remaining-queue). See Actor::scripted_path_waypoints doc.
void applyScriptedPathFromDecl(Actor& e, const EnemySpawnDecl& decl)
{
    std::vector<glm::vec3> full_path = decl.scripted_path_waypoints;
    if (decl.scripted_target_pos.has_value())
        full_path.push_back(*decl.scripted_target_pos);
    if (full_path.empty())
        return;
    e.scripted_target_pos = full_path.front();
    e.scripted_path_waypoints.assign(full_path.begin() + 1, full_path.end());
    e.scripted_stop_range = decl.scripted_stop_range;
    e.had_scripted_target = true;
}

// Resolve archetype.spawn_clip || idle_clip into (clip, key) for the
// initial loco bind.
struct PrimeClipResolution
{
    const selva::anim::AnimationClip* clip = nullptr;
    const char* key = nullptr;
};

PrimeClipResolution resolvePrimeClip(const Actor& e)
{
    PrimeClipResolution out;
    if (e.archetype != nullptr && !e.archetype->spawn_clip.empty())
    {
        const auto& reg = selva::anim::clipsByKey(e.skeleton_id);
        const selva::anim::AnimationClip* c = reg.get(e.archetype->spawn_clip);
        if (c != nullptr && c->isLoaded())
        {
            out.clip = c;
            out.key = e.archetype->spawn_clip.c_str();
            return out;
        }
        selva::combat::combatLog(
            "[spawn] spawn_clip '{}' not found on skeleton '{}' (actor '{}'); falling back to idle",
            e.archetype->spawn_clip, e.skeleton_id, e.spawn_id);
    }
    const ClipLookup spawn_idle = lookupArchetypeClip(e, ClipFamily::PeacefulIdle);
    out.clip = spawn_idle.clip;
    out.key = spawn_idle.key;
    return out;
}

// Fire the optional initial-freeze one-shot for held-pose spawns.
// Pattern A (initial_state + initial_freeze_at_seconds, e.g. Lupa) or
// Pattern B (spawn_clip + spawn_clip_freeze_at_seconds, e.g. feeders).
void firePoseHeldOneShot(Actor& e, const selva::anim::AnimationClip& clip, const char* clip_key)
{
    if (e.archetype == nullptr)
        return;
    const bool pattern_a = e.archetype->spawn_clip.empty() &&
                           e.archetype->initial_freeze_at_seconds > 0.0f &&
                           !e.archetype->initial_state.empty();
    const bool pattern_b =
        !e.archetype->spawn_clip.empty() && e.archetype->spawn_clip_freeze_at_seconds > 0.0f;
    if (!pattern_a && !pattern_b)
        return;
    const float freeze_at = pattern_a ? e.archetype->initial_freeze_at_seconds
                                      : e.archetype->spawn_clip_freeze_at_seconds;
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = clip_key;
    opts.freeze_last = true;
    opts.freeze_at_seconds = freeze_at;
    e.sampler.playOneShot(clip, /*blend_in_seconds=*/0.0f, /*blend_out_seconds=*/0.20f,
                          selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time_seconds=*/freeze_at, /*playback_rate=*/1.0f, opts);
}

void primeSpawnPose(Actor& e)
{
    // Bind the loco track to spawn_clip or idle_clip, whichever the
    // archetype declares. Without this the first rendered frame is
    // the standing idle even for actors whose spawn pose is prone
    // (larva_fresh in zombie_crawl). See feedback memory for why the
    // key MUST flow through to sampler.update.
    const PrimeClipResolution prime = resolvePrimeClip(e);
    if (prime.clip == nullptr || !prime.clip->isLoaded())
    {
        selva::combat::combatLog("[spawn] no prime clip '{}' on skeleton '{}' (actor '{}')",
                                 prime.key ? prime.key : "(none)", e.skeleton_id, e.spawn_id);
        return;
    }
    e.sampler.update(*prime.clip, 0.0f, 0.0f, /*loops=*/true, prime.key);
    firePoseHeldOneShot(e, *prime.clip, prime.key);
}

// Phase-stagger AI tick, create Jolt body, push into pool, fire dormant
// state init if Pattern B.
void finalizeSpawnActor(Actor& e)
{
    const int pool_index = static_cast<int>(actors().size());
    seedAiTickPhase(e, pool_index, selva::tuning::current());
    const bool needs_dormant_init =
        (e.archetype != nullptr) && e.archetype->is_boss && !e.archetype->initial_state.empty();
    e.character_body =
        selva::world::createCharacterBody(e.pos, e.body.collider_radius, e.body.collider_height);
    actors().push_back(std::move(e));
    if (needs_dormant_init)
    {
        // setBossState skips no-op transitions; nudge through Engaged
        // so the Dormant funnel mirror writes always run.
        Actor& pooled = actors()[pool_index];
        pooled.boss_state = BossState::Engaged;
        setBossState(pooled, BossState::Dormant);
    }
}
} // namespace

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
                        "[spawn] boss '{}' skipped at boot (already felled this save)", decl.id);
                    return;
                }
            }
        }
    }
    if (!decl.spawn_trigger_id.empty())
    {
        // Trigger-spawned: skip at boot. The custom-trigger dispatcher
        // will call spawnEnemyFromDecl again when the trigger fires.
        selva::combat::combatLog("[spawn] enemy '{}' deferred (waits on spawn_trigger '{}')",
                                 decl.id, decl.spawn_trigger_id);
        return;
    }

    Actor e;
    initSpawnIdentity(e, region_id, decl, archetype_ptr);
    bindSpawnArchetype(e, decl, archetype_ptr);
    primeSpawnPose(e);
    if (decl.scripted_target_pos.has_value())
        applyScriptedPathFromDecl(e, decl);
    e.boss_state = BossState::Dormant;
    e.current_boss_state.clear();
    finalizeSpawnActor(e);
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
// Yaw-acknowledgment overlay (Souls-style "the NPC notices you walking
// by"). Runs after the BT tick so it only overrides yaw when the actor
// is passive. Caps to archetype->acknowledgment_max_angle_radians so
// the NPC can't turn past their natural viewing arc -- reads as
// natural attentiveness, not tracking.
void tickYawAcknowledgment(Actor& a)
{
    a.yaw_intent_from_acknowledgment = false;
    if (a.archetype == nullptr || a.archetype->face_player_range_meters <= 0.0f)
        return;
    if (a.perception.awareness >= Awareness::Combat || !std::isnan(a.scripted_target_pos.x))
        return;
    const Actor& pc = player();
    const float dx = pc.pos.x - a.pos.x;
    const float dz = pc.pos.z - a.pos.z;
    const float dist_sq = dx * dx + dz * dz;
    const float range = a.archetype->face_player_range_meters;
    if (dist_sq > range * range)
        return;
    const float desired = yawFacing(a.pos, pc.pos);
    const float max_angle = a.archetype->acknowledgment_max_angle_radians;
    if (max_angle <= 0.0f)
    {
        a.turn_intent_yaw = desired;
        a.yaw_intent_from_acknowledgment = true;
        return;
    }
    constexpr float kPi = 3.1415927f;
    constexpr float kTwoPi = 6.2831853f;
    float delta = desired - a.spawn_yaw;
    while (delta > kPi)
        delta -= kTwoPi;
    while (delta < -kPi)
        delta += kTwoPi;
    if (delta > max_angle)
        delta = max_angle;
    else if (delta < -max_angle)
        delta = -max_angle;
    a.turn_intent_yaw = a.spawn_yaw + delta;
    a.yaw_intent_from_acknowledgment = true;
}

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

    tickYawAcknowledgment(a);

    if (selva::debug::flags().ai_decision_log)
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
namespace
{
// True if the current loco clip drives world translation via
// consumedHipDelta -- in which case velocity integration must NOT
// also run or they compound (visible foot-skate / "moves at 2x speed").
bool isLocoRootMotionDriven(const Actor& a)
{
    const auto fd = a.sampler.frameDiagnostics();
    const char* cur_name = fd.loco_current_name;
    const selva::anim::TranslationSource src =
        (cur_name != nullptr) ? selva::anim::locomotionConfig().translationSource(cur_name)
                              : selva::anim::TranslationSource::Velocity;
    return src == selva::anim::TranslationSource::RootMotion;
}

void rampVelocityToward(Actor& a, float dt, const selva::tuning::Tunables& tun)
{
    const glm::vec2 delta = a.intent_xz - a.velocity_xz;
    const float delta_mag = glm::length(delta);
    if (delta_mag <= 0.0001f)
        return;
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

// Belt-and-braces velocity clamp: even if some other system writes a
// velocity that would carry the actor into an AI barrier or a hazard
// it should avoid, this clears the offending axis BEFORE Jolt
// integrates. Consulted axis-by-axis so a barrier on X still lets
// the actor slide along Z.
void clampVelocityAgainstHazards(Actor& a, float dt)
{
    if (a.archetype == nullptr || a.archetype->avoids_hazards.empty())
        return;
    const auto& avoided = a.archetype->avoids_hazards;
    const float new_x = a.pos.x + a.velocity_xz.x * dt;
    const float new_z = a.pos.z + a.velocity_xz.y * dt;
    const glm::vec3 try_x(new_x, a.pos.y, a.pos.z);
    const glm::vec3 try_z(a.pos.x, a.pos.y, new_z);
    if (selva::hazard::positionIsInAvoidedZone(try_x, avoided))
        a.velocity_xz.x = 0.0f;
    if (selva::hazard::positionIsInAvoidedZone(try_z, avoided))
        a.velocity_xz.y = 0.0f;
}

// Edge-triggered diagnostic: log when an actor with avoids_hazards
// crosses into/out of one of its avoided zones. Gated by
// selva::debug::flags().enemy_lifecycle.
void traceHazardEntryExit(Actor& a)
{
    const std::vector<std::string>* avoided =
        (a.archetype != nullptr && !a.archetype->avoids_hazards.empty())
            ? &a.archetype->avoids_hazards
            : nullptr;
    const bool in_hazard_now =
        (avoided != nullptr) && selva::hazard::positionIsInAvoidedZone(a.pos, *avoided);
    if (in_hazard_now == a.was_in_avoided_hazard)
        return;
    if (selva::debug::flags().enemy_lifecycle)
    {
        std::fprintf(stderr, "[hazard-violation] %s arch=%s %s pos=(%.2f,%.2f,%.2f)\n",
                     a.spawn_id.c_str(), a.archetype ? a.archetype->id.c_str() : "(null)",
                     in_hazard_now ? "ENTER" : "EXIT", a.pos.x, a.pos.y, a.pos.z);
        std::fflush(stderr);
    }
    a.was_in_avoided_hazard = in_hazard_now;
}

void stepYawTowardIntent(Actor& a, float dt, const selva::tuning::Tunables& tun)
{
    constexpr float kTwoPi = 6.2831853f;
    constexpr float kPi = 3.1415927f;
    float yaw_delta = a.turn_intent_yaw - a.yaw;
    while (yaw_delta > kPi)
        yaw_delta -= kTwoPi;
    while (yaw_delta < -kPi)
        yaw_delta += kTwoPi;
    // Acknowledgment-driven yaw uses the slow rate (per-archetype
    // scale, default 0.15 = ~50deg/s); all other intents
    // (combat / scripted-walk / idle-to-spawn) use the full rate
    // (default 6.0 rad/s = ~344deg/s). The scale is read off the
    // archetype so per-NPC tuning is possible.
    float rate = tun.ai_turn_rate_radians_per_sec;
    if (a.yaw_intent_from_acknowledgment && a.archetype != nullptr)
        rate *= a.archetype->acknowledgment_turn_rate_scale;
    const float max_yaw_step = rate * dt;
    if (std::abs(yaw_delta) <= max_yaw_step)
        a.yaw = a.turn_intent_yaw;
    else
        a.yaw += (yaw_delta > 0.0f ? max_yaw_step : -max_yaw_step);
}
} // namespace

void tickEnemyLocomotion(Actor& a, float dt, const selva::tuning::Tunables& tun)
{
    // Movement-lock rules (see field comments on each):
    //   * action_locks_movement: hard lock for the full one-shot.
    //     Heavy swings where the body MUST stay planted (wolf bite).
    //   * cancel_fraction: soft lock until past cancel; ramp resumes
    //     WHILE recovery plays. Light tracking attacks.
    //   * Root-motion loco clip: always zero (clip drives translation,
    //     velocity must not also integrate -- universal physics
    //     refactor's hip-delta vs velocity contract).
    const bool root_motion_clip = isLocoRootMotionDriven(a);
    const bool one_shot_active = a.sampler.isOneShotActive();
    const bool hard_lock = one_shot_active && a.action_locks_movement;
    const bool soft_lock = one_shot_active && !a.sampler.isOneShotPastCancelFraction();
    const bool gated = hard_lock || soft_lock || root_motion_clip;
    if (!one_shot_active)
        a.action_locks_movement = false;
    if (gated)
    {
        // Yaw still ticks below (turn-while-rooted is correct -- the
        // wolf rotates her body to face the player while authored
        // walk drives forward translation).
        a.velocity_xz = glm::vec2(0.0f);
    }
    else
    {
        rampVelocityToward(a, dt, tun);
        clampVelocityAgainstHazards(a, dt);
    }
    traceHazardEntryExit(a);
    // Y is owned by Jolt's gravity in updatePhysics. The earlier
    // groundHeight snap got removed (universal-actor-physics refactor):
    // it produced teleport-up bugs in the vertical-stack world.
    stepYawTowardIntent(a, dt, tun);
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
// Visual/state-only restoration of a previously-felled actor on
// save-load. Snaps to the final frame of the death clip (no blend,
// no audio, no scene-begin, no profile-list-append, no rewards). The
// real-death-event path (fireEnemyDeath) owns those side effects;
// this is the "the cosmology already knows this actor is dead --
// just render them that way" path.
namespace
{
// Resolve the death clip for an actor. Per-actor death_clip_name
// override wins (player uses "second_death"); falls through to the
// archetype's Death family on the per-skeleton registry. clip_name
// stays the empty string when nothing resolves.
struct DeathClipResolution
{
    const selva::anim::AnimationClip* clip = nullptr;
    const char* key = "";
};

DeathClipResolution resolveDeathClip(const Actor& e)
{
    DeathClipResolution out;
    const auto& reg = selva::anim::clipsByKey(e.skeleton_id);
    if (!e.death_clip_name.empty())
    {
        const auto* c = reg.get(e.death_clip_name);
        if (c != nullptr && c->isLoaded())
        {
            out.clip = c;
            out.key = e.death_clip_name.c_str();
            return out;
        }
    }
    const ClipLookup fallback = lookupArchetypeClip(e, ClipFamily::Death);
    out.clip = fallback.clip;
    out.key = fallback.key;
    return out;
}
} // namespace

void reapplyDeadPose(Actor& e)
{
    const DeathClipResolution dc = resolveDeathClip(e);
    const selva::anim::AnimationClip* death_clip = dc.clip;
    const char* clip_name = dc.key;
    if (death_clip != nullptr && death_clip->isLoaded())
    {
        selva::anim::PoseSampler::OneShotOptions opts;
        opts.clip_key = clip_name;
        opts.freeze_last = true;
        // Snap to the last frame (no blend, no audio, no flop).
        e.sampler.playOneShot(*death_clip, /*blend_in_seconds=*/0.0f,
                              /*blend_out_seconds=*/0.0f, selva::anim::PoseSampler::BodyMask::Full,
                              /*start_time_seconds=*/death_clip->duration(),
                              /*playback_rate=*/1.0f, opts);
    }
    e.is_dead = true;
    e.death_time = 0.0f; // sentinel; not the real death timestamp
    // Save-load restored a previously-felled actor: tear down its
    // Jolt body so the corpse doesn't participate in physics. Same
    // contract as fireEnemyDeath.
    selva::world::destroyCharacterBody(e.character_body);
    e.character_body = engine::physics::BodyHandle{};
    if (e.is_boss)
        setBossState(e, BossState::Felled);
}

namespace
{
void fireDeathClipAndAudio(Actor& e, const selva::anim::AnimationClip& death_clip,
                           const char* clip_name)
{
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = clip_name;
    opts.freeze_last = true;
    e.sampler.playOneShot(death_clip, /*blend_in_seconds=*/0.25f,
                          /*blend_out_seconds=*/0.25f, selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    if (!e.death_sfx_name.empty())
    {
        selva::combat::combatLog("[death-audio] bed sfx='{}'", e.death_sfx_name);
        selva::audio::playSfx(e.death_sfx_name);
    }
    for (const auto& sfx_name : e.death_peak_sfx_names)
    {
        const float peak_offset = selva::audio::sfxPeakOffset(sfx_name);
        const float play_at = e.death_time + e.death_peak_align_seconds - peak_offset;
        selva::combat::combatLog(
            "[death-audio] scheduled sfx='{}' peak_off={:.3f}s align={:.3f}s play_at={:.3f}",
            sfx_name, peak_offset, e.death_peak_align_seconds, play_at);
        selva::audio::scheduleSfx(sfx_name, play_at);
    }
}

void persistFelledBoss(Actor& e)
{
    if (!e.is_boss || e.spawn_decl_id.empty())
        return;
    // setBossState(Felled) is THE funnel: clears current_boss_state,
    // active_boss_idx/id, pops the audio bed, logs the transition.
    setBossState(e, BossState::Felled);
    PlayerProfile* profile = selva::activePlayerProfile();
    if (profile != nullptr)
    {
        const auto& fb = profile->felled_bosses;
        if (std::find(fb.begin(), fb.end(), e.spawn_decl_id) == fb.end())
        {
            profile->felled_bosses.push_back(e.spawn_decl_id);
            selva::combat::combatLog("[boss-felled] '{}' appended to profile '{}'.felled_bosses",
                                     e.spawn_decl_id, profile->name);
        }
    }
    // Reward dispatch. v1 assumes the player killed the boss; routing
    // through hit-detection's owner field will refine attribution
    // later when faction-conflict ships.
    selva::gameplay::onBossFelled(e.spawn_decl_id, selva::gameplay::player());
}

// Spawn the sangue-magnetization pulse from the dead actor's body
// center to the Vagrant's vessel. Skips player-self deaths (no
// self-grant on second death) and zero-drop archetypes. Body center
// uses the archetype's collider_height so quadrupeds anchor at their
// true mid-body rather than the feet -- substrate-honest, no
// per-archetype tuning needed.
void queueSangueOnKill(const Actor& e)
{
    if (e.controller == Controller::Input || e.archetype == nullptr ||
        e.archetype->sangue_drop == 0u)
        return;
    const glm::vec3 body_center = e.pos + glm::vec3(0.0f, e.body.collider_height * 0.5f, 0.0f);
    selva::gameplay::queueSangueDrop(body_center, e.archetype->sangue_drop);
}

// Process-wide RNG for loot rolls. Seeded once at first use from
// std::random_device so repeat playthroughs differ; subsequent rolls
// thread through the same generator so the sequence is reproducible
// within a run (useful for save-scum debugging). Intentionally NOT
// in the deterministic path -- combat outcomes that need replay
// stability use their own seeded generators elsewhere.
std::mt19937& lootRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

// Roll the archetype's loot table and spawn a world pickup for each
// successful drop. Per [[project_items_loot_doctrine_locked]] LCK
// scales drop chance via the engine FormulaConfig.luck.drop_scale
// knob (default 15 -- each LCK point gives +15% relative bonus,
// capped at 100% effective). Quantity is rolled uniformly in
// [min_qty, max_qty]. Pickup spawns at the corpse position with a
// small XZ scatter so multi-drop kills don't visually overlap; Y
// snaps to terrain height under that XZ. Skips player deaths and
// archetypes with empty loot tables.
void rollAndSpawnLoot(const Actor& e)
{
    if (e.controller == Controller::Input || e.archetype == nullptr ||
        e.archetype->loot_drops.empty())
        return;

    const int lck = selva::gameplay::player().stats.lck;
    const float drop_scale = selva::formulas::current().luck.drop_scale;

    std::uniform_real_distribution<float> chance_roll(0.0f, 1.0f);

    for (const auto& drop : e.archetype->loot_drops)
    {
        const float effective = std::min(
            1.0f, drop.base_chance * (1.0f + static_cast<float>(lck) * drop_scale / 100.0f));
        if (chance_roll(lootRng()) >= effective)
            continue;

        const int qty =
            (drop.max_qty > drop.min_qty)
                ? std::uniform_int_distribution<int>(drop.min_qty, drop.max_qty)(lootRng())
                : drop.min_qty;
        if (qty <= 0)
            continue;

        engine::ecs::ItemInstance instance;
        instance.config_path = drop.config_path;
        instance.quantity = qty;

        // Spawn directly on the corpse's hips -- no scatter. The
        // overlap case where multiple pickups stack on each other is
        // resolved by the interaction-cycle system (Tab to switch
        // between overlapping interactables), not by separating their
        // world positions. world_pos here is just the fallback for
        // when the source actor goes away; the live position is
        // resolved each frame from the actor's hips joint in
        // selva::loot::livePickupPos.
        glm::vec3 pos = e.pos;
        pos.y = e.pos.y + e.body.collider_height * 0.5f;

        const auto pickup_id = selva::loot::spawnPickup(pos, instance, e.spawn_decl_id);
        if (pickup_id == selva::loot::kInvalidId)
            continue;

        std::fprintf(stderr, "[loot] dropped %s x%d (chance %.2f%% eff, LCK %d)\n",
                     drop.config_path.c_str(), qty, effective * 100.0f, lck);
        std::fflush(stderr);
    }
}
} // namespace

void fireEnemyDeath(Actor& e, int index)
{
    const DeathClipResolution dc = resolveDeathClip(e);
    if (dc.clip != nullptr && dc.clip->isLoaded())
    {
        e.death_time = selva::wallClock();
        fireDeathClipAndAudio(e, *dc.clip, dc.key);
    }
    else
    {
        e.death_time = selva::wallClock();
    }
    // Corpses are PERMANENT by default -- Souls / Elden Ring
    // convention: a body that fell stays where it fell. The audio +
    // death clip fired on the real wall-clock time above; the
    // alpha-fade clock is then zeroed (sentinel) so
    // computeEnemyDeathFadeAlpha returns 1.0 forever. The only path
    // that actually starts a fade is the soul-larvae feeder cycle:
    // FlowSpawner::consumePairedCorpse rewrites death_time back to a
    // recent wallClock value when a paired fresh arrives to feed.
    // Other future "corpse vanishes" mechanics opt in by similarly
    // writing a real death_time onto the actor.
    e.is_dead = true;
    e.death_time = 0.0f;
    // Tear down the Jolt character body -- corpses are visual-only
    // (death clip freezes the pose); they no longer participate in
    // physics. Per the real-physics doctrine.
    selva::world::destroyCharacterBody(e.character_body);
    e.character_body = engine::physics::BodyHandle{};
    // Player-only: duck the OST so the death audio reads clean against
    // a muffled background. Restored on respawn in
    // tickPlayerSecondDeathLifecycle.
    if (e.controller == Controller::Input)
        selva::audio::duckMusic();
    persistFelledBoss(e);
    if (e.archetype != nullptr && !e.archetype->felled_flag.empty())
        setFlag(e.archetype->felled_flag);
    // Insight: this archetype was killed. Bumps the profile's
    // kill_counts map; insight::tick() will fire any kill_count
    // trigger whose threshold this puts us at.
    if (e.archetype != nullptr && !e.archetype->id.empty())
        selva::insight::notifyKill(e.archetype->id);
    queueSangueOnKill(e);
    rollAndSpawnLoot(e);
    // No Scene is opened on death. The death clip plays freely on the
    // corpse; the player keeps movement and look. Previously the
    // Guide-rescue handler took over the post-death input-lock to
    // walk the Guide out of the chapel; that handoff is gone in the
    // new flow (the player drives the chapel-door + Guide-talk
    // sequence themselves). Without a follow-up handler the Scene
    // would never end and the player would freeze. If a future
    // cinematic moment wants to lock input on a specific death, it
    // should open its own Scene + define its own end condition.
    selva::combat::combatLog("[death] actor[{}] died (clip={})", index, dc.key);
}

bool applyArchetypeSwap(Actor& a, const EnemyArchetype& target)
{
    if (a.is_dead)
        return false;
    const std::string prev_id = (a.archetype != nullptr) ? a.archetype->id : std::string("(none)");
    // Snapshot collider dims pre-swap so we can detect whether the
    // new archetype's form (DamnedSoul / Animal / etc.) changed the
    // Jolt capsule. The Jolt body is created at spawn from these
    // values; applyArchetypeToActor -> applyFormDefaults will rewrite
    // them. Without a rebuild the live Jolt capsule keeps the OLD
    // shape while the actor struct says the new one -- dual-source-
    // of-truth drift waiting to bite a cross-form swap.
    const float prev_radius = a.body.collider_radius;
    const float prev_height = a.body.collider_height;
    // THE archetype-to-actor funnel: same call as spawn. Re-derives
    // every archetype-driven field (faction, form, pools, hurtboxes,
    // interactable, is_boss, is_npc). Adding a new archetype-derived
    // field updates ONE site -- not two.
    applyArchetypeToActor(a, target);
    // Same-dim case is the common path (DamnedSoul -> DamnedSoul,
    // e.g. larva_fresh -> larva_aged). Skip the rebuild cost. Cross-
    // form swap rebuilds the capsule at the actor's current world pos.
    constexpr float kColliderRebuildEpsilon = 0.001f;
    if (std::fabs(a.body.collider_radius - prev_radius) > kColliderRebuildEpsilon ||
        std::fabs(a.body.collider_height - prev_height) > kColliderRebuildEpsilon)
    {
        selva::world::destroyCharacterBody(a.character_body);
        a.character_body = selva::world::createCharacterBody(a.pos, a.body.collider_radius,
                                                             a.body.collider_height);
    }
    // Cooldowns from the prior archetype's actions are meaningless
    // for the new one. Movement intent + velocity BOTH zeroed so the
    // archetype swap is a true discontinuity: no carryover motion
    // from the previous form, no half-decayed velocity ramp. Without
    // this, the post-swap actor inherits whatever velocity_xz was on
    // the actor at the moment of swap, plus the hip-delta-discontinuity
    // that fires when a new clip starts (zombie_scream's hip is at a
    // different absolute position than zombie_crawl's, so the first
    // consumedHipDelta() after the new one-shot starts is a huge
    // jump). Both inputs to velocity get reset; the BT picks fresh
    // movement on the next tick from a clean zero state.
    a.action_state.clear();
    a.intent_xz = glm::vec2(0.0f);
    a.velocity_xz = glm::vec2(0.0f);
    // Clear any per-actor loco override the prior life set (e.g. the
    // larva-fresh feeding clip). The new archetype's gait picker takes
    // over from this frame on.
    a.idle_clip_override.clear();
    // Aggro-clip: fire it immediately on swap so the transition has
    // a visual cue. Mark aggro_already_fired so the perception
    // Suspicious->Alerted hook won't double-fire it on the actor's
    // first sighting moments later.
    a.aggro_already_fired = true;
    if (!target.aggro_clip.empty())
    {
        const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
        const selva::anim::AnimationClip* clip = reg.get(target.aggro_clip);
        if (clip != nullptr && clip->isLoaded())
        {
            selva::anim::PoseSampler::OneShotOptions opts;
            opts.clip_key = target.aggro_clip.c_str();
            opts.cancel_fraction = 1.0f;
            a.sampler.playOneShot(*clip, /*blend_in_seconds=*/0.20f, /*blend_out_seconds=*/0.20f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time_seconds=*/0.0f,
                                  /*playback_rate=*/1.0f, opts);
            a.action_locks_movement = true;
        }
    }
    // After playOneShot, the first consumedHipDelta() may report a
    // large value (discontinuity between the prior loco clip's hip
    // and the new one-shot's frame-0 hip). Consume + discard it now
    // so the next applyActorClipHipDelta call sees a fresh zero.
    // Per [[feedback_hip_delta_two_sides]] -- the extract side leaves
    // residual that the apply side would otherwise turn into a slide.
    (void)a.sampler.consumedHipDelta();
    if (selva::debug::flags().enemy_lifecycle)
    {
        std::fprintf(stderr,
                     "[archetype-swap] %s '%s' -> '%s' at t=%.3f  hp=%d/%d  poise=%.1f/%.1f\n",
                     a.spawn_id.c_str(), prev_id.c_str(), target.id.c_str(), selva::wallClock(),
                     a.hp.current, a.hp.max, a.poise.current, a.poise.max);
        std::fflush(stderr);
    }
    return true;
}

void spawnRegionEnemies(const std::string& region_id, const std::vector<EnemySpawnDecl>& decls)
{
    for (const auto& d : decls)
        spawnEnemyFromDecl(region_id, d);
    selva::combat::combatLog("[spawn-region] region='{}' spawned {} enemy actor(s)", region_id,
                             decls.size());
}

// Tear down every non-player actor: destroy Jolt bodies, unregister
// interactables, erase from the pool. Used by both shutdownHubEnemies
// (process exit) and resetCycleEnemies (cycle reset rebuilds from
// authored data instead of resetting in place).
static void clearNonPlayerActors()
{
    auto& pool = actors();
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;
        if (a.interactable_id != selva::interact::kInvalidId)
        {
            selva::interact::unregisterInteractable(a.interactable_id);
            a.interactable_id = selva::interact::kInvalidId;
        }
        selva::world::destroyCharacterBody(a.character_body);
        a.character_body = engine::physics::BodyHandle{};
    }
    pool.erase(std::remove_if(pool.begin() + (pool.empty() ? 0 : 1), pool.end(),
                              [](const Actor& a) { return a.controller != Controller::Input; }),
               pool.end());
}

void shutdownHubEnemies()
{
    clearNonPlayerActors();
}

void resetCycleEnemies()
{
    // Rebuild-from-authored: the doctrine is that save/load + cycle
    // reset both restore the world to its pristine spawn state.
    // Resetting actors in place left transient session state stuck
    // (dynamically-spawned trickle larvae, stale FIFO entries, etc.).
    // Instead, tear down the pool and re-fire the same spawn paths
    // boot uses. Anything that should survive must be derivable from
    // authored data (region JSON, flow JSON) or persisted save state
    // (PlayerProfile.felled_bosses / .flags / .door_states).
    // Per [[feedback_spawn_systems_need_idempotency]].
    //
    // Phase 1: tear down every non-player actor. Destroys Jolt bodies,
    // unregisters interactables, erases pool slots.
    clearNonPlayerActors();
    // Phase 2: re-spawn from authored data. spawnAllRegionEnemies
    // walks every loaded region and re-fires its enemy_spawns.
    // FlowSpawner's tickInitialFill fires automatically on the next
    // tick because resetFlowSpawner cleared initial_fill_done.
    selva::world::spawnAllRegionEnemies();
    // Phase 3: re-apply the active profile's felled_bosses on top of
    // the freshly-spawned world. reapplyDeadPose puts each felled
    // boss back into its death pose without re-firing audio / Scene /
    // reward effects.
    const PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return;
    for (auto& a : actors())
    {
        if (a.controller == Controller::Input || !a.permanent_on_death || a.spawn_decl_id.empty())
            continue;
        const auto& fb = profile->felled_bosses;
        if (std::find(fb.begin(), fb.end(), a.spawn_decl_id) == fb.end())
            continue;
        if (selva::debug::flags().enemy_lifecycle)
        {
            std::fprintf(stderr, "[reset-cycle] '%s' in profile.felled_bosses -> reapplyDeadPose\n",
                         a.spawn_decl_id.c_str());
            std::fflush(stderr);
        }
        reapplyDeadPose(a);
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
// skeleton registry. Enemies pass LocoTier::Jog (no sprint), so jog
// keys map to Run family (the per-archetype "fast" override slot --
// wolves remap that to "gallop").
std::optional<ClipFamily> familyFromDirectionalKey(const char* k)
{
    if (k == nullptr)
        return std::nullopt;
    const std::string s(k);
    if (s == "walking")
        return ClipFamily::Walk;
    if (s == "walking_backward")
        return ClipFamily::WalkBack;
    if (s == "strafe_walking_left")
        return ClipFamily::StrafeLeft;
    if (s == "strafe_walking_right")
        return ClipFamily::StrafeRight;
    if (s == "jogging")
        return ClipFamily::Run;
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
namespace
{
ClipLookup pickGaitBaseClip(const Actor& a, bool& out_is_moving)
{
    const ClipLookup walk = lookupArchetypeClip(a, ClipFamily::Walk);
    const ClipLookup run = lookupArchetypeClip(a, ClipFamily::Run);
    const ClipLookup combat_idle = lookupArchetypeClip(a, ClipFamily::CombatIdle);
    const ClipLookup peaceful_idle = lookupArchetypeClip(a, ClipFamily::PeacefulIdle);
    // Substitute intent magnitude for velocity when the loco clip is
    // RootMotion (velocity is zeroed by tickEnemyLocomotion in that
    // case, so the picker would never see motion). Otherwise root-
    // motion gait actors get stuck in idle.
    const float speed = std::max(glm::length(a.velocity_xz), glm::length(a.intent_xz));
    out_is_moving = (speed > kEnemyWalkSpeedFloor);
    const bool is_running = out_is_moving && (speed >= kEnemyRunSpeedFloor) &&
                            run.clip != nullptr && run.clip->isLoaded();
    const bool is_walking = out_is_moving && walk.clip != nullptr && walk.clip->isLoaded();
    // PeacefulIdle is the actor's pre-disturbance cosmological pose
    // (feeders biting at the pile, kneelers praying, etc.). Once the
    // actor has crossed into Alerted EVER, they don't go back -- even
    // if perception decays back to Unaware later. aggro_already_fired
    // captures "has been disturbed" for the actor's lifetime. Without
    // this, a feeder who lost sight of the player would revert to
    // biting the pile, which is cosmologically wrong (the disturbed
    // soul doesn't resume feeding; it stands and waits, agitated).
    const bool engaged_now = a.perception.awareness >= Awareness::Alerted;
    const bool ever_disturbed = a.aggro_already_fired;
    const bool use_combat_idle = engaged_now || ever_disturbed;
    return is_running ? run : is_walking ? walk : (use_combat_idle ? combat_idle : peaceful_idle);
}

// Strafe/back override on top of the base gait pick. ONLY fires when
// intent has meaningful lateral component -- pure forward chase
// preserves the run/walk base, so chasing actors don't get downgraded
// every frame to a walk-family directional clip.
EnemyLocoPick maybeApplyDirectionalOverride(const Actor& a, EnemyLocoPick base_pick)
{
    if (a.lock_target_idx < 0)
        return base_pick;
    const float yaw = a.turn_intent_yaw;
    const glm::vec3 fwd(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    const glm::vec3 intent3(a.intent_xz.x, 0.0f, a.intent_xz.y);
    const float intent_len = glm::length(intent3);
    if (intent_len > 1e-4f)
    {
        const glm::vec3 intent_dir = intent3 / intent_len;
        constexpr float kForwardishDot = 0.85f; // ~32deg cone
        if (glm::dot(intent_dir, fwd) >= kForwardishDot)
            return base_pick;
    }
    const char* dir_key = directionalLocoClip(fwd, right, intent3, LocoTier::Jog);
    const auto fam = familyFromDirectionalKey(dir_key);
    if (!fam.has_value())
        return base_pick;
    const ClipLookup dir = lookupArchetypeClip(a, *fam);
    if (dir.clip != nullptr && dir.clip->isLoaded())
        return {dir.clip, dir.key};
    return base_pick;
}
} // namespace

static EnemyLocoPick pickEnemyLocomotionClip(const Actor& a)
{
    // Per-actor override wins over gait-by-speed when set. Used for
    // states where the actor must visibly do a specific thing the
    // normal gait picker wouldn't produce (a fresh larva crouched
    // over a corpse, eating). applyArchetypeSwap clears the override
    // so post-conversion actors resume standard gait.
    if (!a.idle_clip_override.empty())
    {
        const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
        const selva::anim::AnimationClip* clip = reg.get(a.idle_clip_override);
        if (clip != nullptr && clip->isLoaded())
            return EnemyLocoPick{clip, a.idle_clip_override.c_str()};
    }
    bool is_moving = false;
    const ClipLookup base = pickGaitBaseClip(a, is_moving);
    EnemyLocoPick out{base.clip, base.key};
    if (!is_moving)
        return out;
    return maybeApplyDirectionalOverride(a, out);
}

// Per-actor body of tickEnemies. Returns true if the lifecycle
// short-circuited (death/knockdown handled the actor this frame and
// the locomotion path should be skipped). All clip lookups go
// through the per-skeleton funnel -- the wolf's sampler never sees a
// player clip.
static bool tickOneEnemy(Actor& a, const Actor& pc, float dt, const selva::tuning::Tunables& tun)
{
    const Awareness pre_perception_awareness = a.perception.awareness;
    {
        ZoneScopedN("tickPerception");
        tickPerception(a, pc, dt, tun);
    }
    updateEnemyLockOnPlayer(a, pc);
    // Aggro-clip hook: fires once when this actor crosses the
    // Suspicious -> Alerted edge ("confirmed sighting" in the
    // perception state machine; Souls/ER Hollow-wakes-up moment).
    // Movement-locked for the clip's full duration; the BT's chase
    // + attack only begins after the clip's one-shot finishes.
    // archetype.aggro_clip empty = no scream / wake-up; actor goes
    // straight from Alerted into chase. See [[project_soul_larvae_cosmology]].
    if (a.archetype != nullptr && !a.archetype->aggro_clip.empty() && !a.aggro_already_fired &&
        pre_perception_awareness < Awareness::Alerted &&
        a.perception.awareness >= Awareness::Alerted)
    {
        const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
        const selva::anim::AnimationClip* aggro = reg.get(a.archetype->aggro_clip);
        if (aggro != nullptr && aggro->isLoaded())
        {
            selva::anim::PoseSampler::OneShotOptions opts;
            opts.clip_key = a.archetype->aggro_clip.c_str();
            opts.cancel_fraction = 1.0f; // lock for full duration
            a.sampler.playOneShot(*aggro, /*blend_in_seconds=*/0.20f, /*blend_out_seconds=*/0.20f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time_seconds=*/0.0f,
                                  /*playback_rate=*/1.0f, opts);
            a.action_locks_movement = true;
            a.aggro_already_fired = true;
            selva::combat::combatLog("[aggro] {} fired aggro_clip='{}' at t={:.3f}", a.spawn_id,
                                     a.archetype->aggro_clip, selva::wallClock());
        }
        else
        {
            selva::combat::combatLog("[aggro] {} aggro_clip='{}' missing from skeleton '{}'",
                                     a.spawn_id, a.archetype->aggro_clip, a.skeleton_id);
        }
    }
    const bool engaged_now = a.perception.awareness >= Awareness::Alerted;
    const ClipLookup lifecycle_idle =
        lookupArchetypeClip(a, engaged_now ? ClipFamily::CombatIdle : ClipFamily::PeacefulIdle);
    if (tickDeathLifecycle(a, dt, lifecycle_idle.clip, lifecycle_idle.key))
    {
        // Dead actors hold the death clip frozen (in_place); no
        // translation needed. Body was destroyed at fireEnemyDeath.
        return true;
    }
    if (tickKnockdownLifecycle(a, dt, lifecycle_idle.clip, lifecycle_idle.key))
    {
        // Stunned/knockdown clips are in_place; the actor stays put.
        return true;
    }
    if (shouldTickAi(a, tun))
    {
        if (selva::debug::flags().ai_tick_log)
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
    // Hip-delta apply: converts the clip's authored hip-XZ for this
    // frame into a velocity contribution on actor.velocity_xz. The
    // unified post-tick physics pass picks that velocity up and
    // hands it to Jolt; integration happens in updatePhysics, not
    // here. Locomotion's velocity_xz write composes with this hip
    // contribution (root-motion clips zero locomotion velocity so the
    // hip is the only contribution; velocity gait clips don't have
    // significant hip delta to apply).
    applyActorClipHipDelta(a, dt);
    // Re-clamp AFTER hip-delta: tickEnemyLocomotion's clamp only sees
    // velocity-driven actors (root-motion clips zero velocity there).
    // Hip-delta then ADDS the clip's authored translation as velocity,
    // which would otherwise carry root-motion-clip actors (e.g. larvae
    // walking with zombie_walk's authored hip) straight through any
    // AI barrier / avoided hazard zone. Clamp again so the final
    // velocity Jolt sees respects both barrier and hazard boundaries
    // regardless of which path wrote it.
    clampVelocityAgainstHazards(a, dt);
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

namespace
{
// Scripted-death HP drain. Single continuous curve across the
// Engaged-then-Dying window: drops from full to drain_to_fraction
// over Engaged, then drain_to_fraction to 0 over Dying. Driven by
// wallclock, not damage.
void applyScriptedDeathHpDrain(Actor& a, float now)
{
    if (a.archetype == nullptr || a.archetype->scripted_death_drain_to_fraction <= 0.0f)
        return;
    const float drain_start = a.scripted_death_at_wallclock - a.archetype->scripted_death_seconds;
    const float total_window = a.scripted_death_drain_end_wallclock - drain_start;
    const float elapsed = std::clamp(now - drain_start, 0.0f, total_window);
    const float t = total_window > 0.0f ? elapsed / total_window : 1.0f;
    const float engaged_fraction = a.archetype->scripted_death_seconds / total_window;
    const float drain_to = a.archetype->scripted_death_drain_to_fraction;
    float hp_fraction;
    if (t <= engaged_fraction)
    {
        const float t_engaged = engaged_fraction > 0.0f ? t / engaged_fraction : 1.0f;
        const float curve = std::pow(t_engaged, a.archetype->scripted_death_drain_exponent);
        hp_fraction = 1.0f - (1.0f - drain_to) * curve;
    }
    else
    {
        const float dying_span = 1.0f - engaged_fraction;
        const float t_dying = dying_span > 0.0f ? (t - engaged_fraction) / dying_span : 1.0f;
        hp_fraction = drain_to * (1.0f - t_dying);
    }
    const int target_hp = static_cast<int>(std::round(static_cast<float>(a.hp.max) * hp_fraction));
    if (target_hp < a.hp.current)
        a.hp.current = std::max(0, target_hp);
}

void tickScriptedDeathDrainPhase(float now)
{
    for (auto& a : actors())
    {
        if (a.is_dead)
            continue;
        const bool in_scripted_death =
            (a.boss_state == BossState::Engaged || a.boss_state == BossState::Dying);
        if (!in_scripted_death || a.scripted_death_drain_end_wallclock < 0.0f)
            continue;
        applyScriptedDeathHpDrain(a, now);
        if (a.boss_state == BossState::Engaged && now >= a.scripted_death_at_wallclock)
        {
            std::fprintf(stderr,
                         "[scripted-death] '%s' deadline reached at t=%.2f (hp was %d/%d)\n",
                         a.spawn_decl_id.c_str(), now, a.hp.current, a.hp.max);
            std::fflush(stderr);
            beginScriptedDying(a);
        }
    }
}

void tickScriptedDeathPainCompletion(float now)
{
    for (auto& a : actors())
    {
        if (a.is_dead || a.boss_state != BossState::Dying)
            continue;
        if (a.dying_until_wallclock < 0.0f || now < a.dying_until_wallclock)
            continue;
        std::fprintf(stderr, "[scripted-death] '%s' pain stage ended at t=%.2f -> Felled\n",
                     a.spawn_decl_id.c_str(), now);
        std::fflush(stderr);
        fireEnemyDeath(a, enemyIndex(a));
    }
}

// Catches the non-death "abandoned mid-fight" path: a boss in Engaged
// whose perception has dropped below Combat (player out of leash for
// ai_combat_disengage_seconds). Felled is handled in fireEnemyDeath.
void tickBossDisengageWatcher()
{
    for (auto& a : actors())
    {
        if (a.boss_state != BossState::Engaged || a.is_dead)
            continue;
        if (a.perception.awareness < Awareness::Combat)
            setBossState(a, BossState::Disengaged);
    }
}
} // namespace

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
    // Scripted-death watchers run as two phases. Engaged -> Dying
    // fires either from natural-deadline expiry OR from the player
    // floor in PerFrameTick calling beginScriptedDying directly --
    // same transition function either way. Dying -> Felled when
    // dying_until_wallclock passes routes through fireEnemyDeath,
    // which plays the death clip and sets the felled_flag. Damage
    // death (HP -> 0) routes through fireEnemyDeath directly,
    // skipping the pain stage (own visual via hit-react).
    const float now = selva::wallClock();
    tickScriptedDeathDrainPhase(now);
    tickScriptedDeathPainCompletion(now);
    tickBossDisengageWatcher();
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
