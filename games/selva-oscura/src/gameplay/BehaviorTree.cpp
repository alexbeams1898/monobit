#include "gameplay/BehaviorTree.h"

#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "combat/CombatLog.h"
#include "combat/HitVolumes.h"
#include "debug/Flags.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"
#include "hazard/HazardZones.h"
#include "world/Territory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

namespace
{

// Forward yaw such that actor faces target. Convention: yaw=0 faces
// -Z. atan2(-dx, -dz) rotates the forward vector (-Z) onto the XZ
// vector (target - actor). Returns 0 if the two are within 1mm so
// no spurious yaw kick at zero distance.
float yawFacing(const glm::vec3& actor_pos, const glm::vec3& target_pos)
{
    const float dx = target_pos.x - actor_pos.x;
    const float dz = target_pos.z - actor_pos.z;
    if ((dx * dx + dz * dz) < 1e-6f)
        return 0.0f;
    return std::atan2(-dx, -dz);
}

// Resolve the effective reach for an action. Priority order:
//   1. effective_reach_override (designer-authored, rare)
//   2. resolved_effective_reach (computed at archetype-load from clip
//      geometry + hitbox_radius + hitbox_tip_offset_z)
//   3. 0 = unresolved; caller decides fallback semantics
// See [[feedback_action_range_max_is_chase_stop_range]] for why this
// is one number used by BOTH actionLegal (fire-gate) and
// computeStopRange (chase-stop).
float effectiveActionRange(const EnemyAction& a)
{
    if (a.effective_reach_override > 0.0f)
        return a.effective_reach_override;
    return a.resolved_effective_reach; // 0 if unresolved
}

// True if `a` is firable by `actor` right now: awareness gate met,
// in range, off cooldown, weight > 0. Extracted from LeafPickAction
// so the filter loop reads as one predicate per candidate.
bool actionLegal(const Actor& actor, const EnemyAction& a, float dist_to_target, float now)
{
    if (actor.perception.awareness < a.min_awareness)
        return false;
    if (a.cooldown_seconds <= 0.0f) // declarative-only, not fire-and-forget
        return false;
    if (dist_to_target < a.range_min)
        return false;
    const float reach = effectiveActionRange(a);
    if (reach > 0.0f && dist_to_target > reach)
        return false;
    if (a.weight <= 0.0f)
        return false;
    const auto it = actor.action_state.find(a.id);
    return it == actor.action_state.end() || now >= it->second.cooldown_until_time;
}

// Fire the picked action's one-shot + spawn (or schedule) its hitbox
// if declared. Returns the spawned hitbox id (0 if deferred via
// windup or no hitbox). Logs the fire.
//
// Windup model (Souls-style attack timing):
//   windup_seconds  -- delay from one-shot fire to hitbox spawn (the
//                      tell; player's dodge window)
//   active_seconds  -- hitbox lifetime once spawned (the strike)
//   recovery        -- implicit: clip continues past spawn+active
//                      with no hitbox; player's punish window
// When windup_seconds == 0 (legacy actions) the spawn is immediate
// and lifetime falls back to the existing lifetime_fraction calc.
std::uint32_t fireAction(Actor& actor, const EnemyAction& picked,
                         const selva::anim::AnimationClip& clip, float dist_to_target, float now)
{
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = picked.clip.c_str();
    opts.freeze_last = picked.freeze_last;
    // cancel_fraction unlocks the velocity-zero gate in tickEnemyLocomotion
    // once the one-shot's elapsed time passes this fraction of duration.
    // Default 1.0 (legacy) = full clip is committed; configure per-action
    // (typically = (windup+active)/clip_duration) for Souls "commit then
    // free to move during recovery animation" feel.
    opts.cancel_fraction = picked.cancel_fraction;
    actor.sampler.playOneShot(clip, picked.blend_in_seconds, picked.blend_out_seconds,
                              selva::anim::PoseSampler::BodyMask::Full,
                              /*start_time_seconds=*/0.0f, picked.playback_rate, opts);
    // Hard movement lock for committed swings (wolf bite: body MUST
    // NOT slide forward during recovery). tickEnemyLocomotion reads
    // this and zeros velocity for the entire one-shot lifetime,
    // ignoring cancel_fraction. Cleared when the one-shot ends.
    actor.action_locks_movement = picked.locks_movement;
    actor.action_state[picked.id].cooldown_until_time = now + picked.cooldown_seconds;
    std::uint32_t hitbox_id = 0;
    if (!picked.hitbox_joint.empty())
    {
        if (picked.windup_seconds > 0.0f)
        {
            // Defer: queue a PendingAttackSpawn that tickPendingAttackSpawns
            // promotes to a live hitbox when wallclock crosses fire_at_time.
            // Single-slot per actor -- overwrite any prior pending swing.
            //
            // windup_seconds + active_seconds are in CLIP-AUTHORED time.
            // Divide by playback_rate to convert to wall-time -- a clip
            // played at 2x speed has its windup pass in half the wall-time.
            const float active = (picked.active_seconds > 0.0f) ? picked.active_seconds : 0.18f;
            const float rate = (picked.playback_rate > 0.0f) ? picked.playback_rate : 1.0f;
            actor.pending_attack.joint_name = picked.hitbox_joint;
            actor.pending_attack.radius = picked.hitbox_radius;
            actor.pending_attack.tip_offset_z = picked.hitbox_tip_offset_z;
            actor.pending_attack.raw_damage = picked.raw_damage;
            actor.pending_attack.poise_damage = picked.poise_damage;
            actor.pending_attack.lifetime_seconds = active / rate;
            actor.pending_attack.fire_at_time = now + picked.windup_seconds / rate;
        }
        else
        {
            // Legacy immediate spawn: hitbox lifetime computed from
            // clip duration * lifetime_fraction inside spawnAttackHitbox.
            selva::combat::AttackHitboxSpawnParams sp;
            sp.actor = &actor;
            sp.attacker =
                selva::combat::OwnerRef{selva::combat::OwnerKind::Enemy, enemyIndex(actor)};
            sp.attacker_faction = actor.faction;
            sp.raw_damage = picked.raw_damage;
            sp.poise_damage = picked.poise_damage;
            sp.joint_name = picked.hitbox_joint.c_str();
            sp.hitbox_radius = picked.hitbox_radius;
            sp.hitbox_tip_offset_z = picked.hitbox_tip_offset_z;
            sp.clip_duration_seconds = clip.duration();
            sp.clip_start_seconds = 0.0f;
            sp.playback_rate = 1.0f;
            sp.mesh_foot_offset_y = actorFootOffsetY(actor);
            hitbox_id = selva::combat::spawnAttackHitbox(sp);
        }
    }
    selva::combat::combatLog(
        "[ai-action] actor picked={} clip={} dist={:.2f} cd_until={:.3f} windup={:.2f}s "
        "active={:.2f}s hitbox_id={}",
        picked.id, picked.clip, dist_to_target, actor.action_state[picked.id].cooldown_until_time,
        picked.windup_seconds, picked.active_seconds, hitbox_id);
    return hitbox_id;
}

// Compute the stop distance for an actor approaching a target.
// Walks until in range of the *closest* awareness-legal action
// declared on the archetype — Souls-style data-driven spacing.
// If no awareness-legal action exists (or no archetype bound),
// falls back to the global engage range so the actor doesn't walk
// unblocked into the target. Per-action range_max is the source of
// truth: when a swing's authored reach is 2m, the actor stops at
// 2m and fires. Adding a longer-range action automatically extends
// the stop distance when that action is the closest legal one.
float computeStopRange(const Actor& actor, const selva::tuning::Tunables& tun)
{
    if (actor.archetype == nullptr)
        return tun.ai_combat_engage_range_meters;
    float closest = -1.0f;
    for (const auto& a : actor.archetype->actions)
    {
        if (actor.perception.awareness < a.min_awareness)
            continue;
        if (a.cooldown_seconds <= 0.0f) // declarative-only
            continue;
        const float reach = effectiveActionRange(a);
        if (reach <= 0.0f) // unresolved AND no override = unconstrained
            continue;
        if (closest < 0.0f || reach < closest)
            closest = reach;
    }
    return (closest > 0.0f) ? closest : tun.ai_combat_engage_range_meters;
}

} // namespace

NodeResult Selector::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    for (auto& child : children)
    {
        if (child->tick(actor, tun) == NodeResult::Success)
            return NodeResult::Success;
    }
    return NodeResult::Failure;
}

NodeResult Sequence::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    for (auto& child : children)
    {
        if (child->tick(actor, tun) == NodeResult::Failure)
            return NodeResult::Failure;
    }
    return NodeResult::Success;
}

NodeResult LeafIdle::tick(Actor& actor, const selva::tuning::Tunables& /*tun*/)
{
    actor.intent_xz = glm::vec2(0.0f);
    actor.turn_intent_yaw = actor.spawn_yaw;
    return NodeResult::Success;
}

NodeResult LeafFollowScriptedTarget::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    // Scripted target inactive if any component is NaN (sentinel).
    if (std::isnan(actor.scripted_target_pos.x) || std::isnan(actor.scripted_target_pos.y) ||
        std::isnan(actor.scripted_target_pos.z))
        return NodeResult::Failure;
    const float stop = actor.scripted_stop_range;
    // Advance through any reached waypoints in a single tick so a
    // dense waypoint chain doesn't take N frames to consume. The
    // FINAL waypoint (empty list) triggers the arrival event.
    while (true)
    {
        const float dx = actor.scripted_target_pos.x - actor.pos.x;
        const float dz = actor.scripted_target_pos.z - actor.pos.z;
        const float dist_sq = dx * dx + dz * dz;
        if (dist_sq > stop * stop)
        {
            const float dist = std::sqrt(dist_sq);
            actor.intent_xz = glm::vec2(dx / dist, dz / dist) * tun.walk_speed;
            actor.turn_intent_yaw = yawFacing(actor.pos, actor.scripted_target_pos);
            return NodeResult::Success;
        }
        // Reached current target. Advance to next waypoint if any.
        if (!actor.scripted_path_waypoints.empty())
        {
            actor.scripted_target_pos = actor.scripted_path_waypoints.front();
            actor.scripted_path_waypoints.erase(actor.scripted_path_waypoints.begin());
            continue; // loop runs again with the new target
        }
        // No more waypoints -- final arrival. Clear to NaN sentinel
        // so spawn-flow / scene systems detect the transition.
        actor.intent_xz = glm::vec2(0.0f);
        actor.turn_intent_yaw = yawFacing(actor.pos, actor.scripted_target_pos);
        actor.scripted_target_pos = glm::vec3(std::numeric_limits<float>::quiet_NaN(),
                                              std::numeric_limits<float>::quiet_NaN(),
                                              std::numeric_limits<float>::quiet_NaN());
        return NodeResult::Success;
    }
}

NodeResult LeafIdleFace::tick(Actor& actor, const selva::tuning::Tunables& /*tun*/)
{
    actor.intent_xz = glm::vec2(0.0f);
    actor.turn_intent_yaw = yawFacing(actor.pos, actor.perception.last_known_player_pos);
    return NodeResult::Success;
}

NodeResult LeafCircleTarget::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    // Archetype opt-out: quadrupeds and other straight-line pursuers
    // (wolf: locomotion IS aggression, no dance) skip this branch
    // entirely and fall through to LeafMoveToTarget below.
    if (actor.archetype != nullptr && actor.archetype->disable_circle_strafe)
        return NodeResult::Failure;
    // Circle ONLY when the target is themselves strafing. Souls/
    // Elden feel: the duel-dance is reactive, not scripted. Default
    // is to charge; circling emerges when the player commits to
    // lateral movement (locking in for the dance). When the player
    // just walks toward/away, the AI closes the gap instead — no
    // robotic timer-driven circling.
    if (actor.lock_target_idx < 0 || actor.duel_strafe_dir == 0)
        return NodeResult::Failure;
    const Actor* target = resolveLockTarget(actor);
    if (target == nullptr)
        return NodeResult::Failure;
    const float dx = target->pos.x - actor.pos.x;
    const float dz = target->pos.z - actor.pos.z;
    const float dist_sq = dx * dx + dz * dz;
    const float engage_range = computeStopRange(actor, tun) * 2.0f;
    if (dist_sq > engage_range * engage_range || dist_sq <= 1e-6f)
        return NodeResult::Failure;
    // "Is target strafing?" — read the target's active loco clip
    // directly. The locomotion picker is the source of truth for
    // what the target IS doing; checking velocity vs intent is
    // brittle (PC zeros velocity_xz when on root-motion clips).
    // Clip name is what the renderer sees and the player perceives.
    const auto fd = target->sampler.frameDiagnostics();
    const char* name = fd.loco_current_name;
    const bool target_strafing = name != nullptr && (std::strstr(name, "strafe_") == name);
    if (!target_strafing)
        return NodeResult::Failure;
    const float dist = std::sqrt(dist_sq);
    // Target is strafing. Mirror with our own strafe at the
    // archetype's combat tempo -- a wolf gallops at chase_speed (6
    // m/s), and the strafe must match that or there's a visible pop
    // when she switches from chase to circle. Humanoid shades leave
    // chase_speed unset and stay at walk_speed (the historic value).
    actor.turn_intent_yaw = yawFacing(actor.pos, target->pos);
    const float sign = static_cast<float>(actor.duel_strafe_dir);
    const glm::vec2 lateral(-dz / dist * sign, dx / dist * sign);
    const float speed = (actor.archetype != nullptr && actor.archetype->chase_speed > 0.0f)
                            ? actor.archetype->chase_speed
                            : tun.walk_speed;
    actor.intent_xz = lateral * speed;
    return NodeResult::Success;
}

// Edge-triggered diagnostic for the territory gate. Logs only when
// the per-actor blocked state changes. Gated by ai_tick_log.
void logTerritoryGateEdge(const Actor& actor, const std::string& player_region, bool blocked)
{
    if (!selva::debug::flags().ai_tick_log)
        return;
    static std::unordered_map<const Actor*, bool> sLastBlocked;
    auto it = sLastBlocked.find(&actor);
    const bool first_see = (it == sLastBlocked.end());
    const bool was_blocked = !first_see && it->second;
    if (first_see || blocked != was_blocked)
    {
        const auto& a = actor.pos;
        const auto& p = actor.perception.last_known_player_pos;
        std::fprintf(stderr,
                     "[territory-gate] '%s' blocked=%d actor_region='%s' "
                     "actor=(%.2f,%.2f,%.2f) player_region='%s' "
                     "player=(%.2f,%.2f,%.2f)\n",
                     actor.spawn_id.c_str(), blocked ? 1 : 0, actor.spawn_region_id.c_str(), a.x,
                     a.y, a.z, player_region.c_str(), p.x, p.y, p.z);
    }
    sLastBlocked[&actor] = blocked;
}

NodeResult LeafMoveToTarget::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    actor.turn_intent_yaw = yawFacing(actor.pos, actor.perception.last_known_player_pos);
    // Hazard-zone avoidance gate. If the target's position lies in a
    // hazard zone whose kind this actor avoids (e.g. damned souls in
    // Acheron), don't chase. The actor stays put and faces the target
    // -- the canonical "eternally awaiting" tableau when the player
    // wades into the river. Per [[project_soul_larvae_cosmology]]
    // river-dissolves-on-contact + selva/hazard/HazardZones.h.
    if (actor.archetype != nullptr && !actor.archetype->avoids_hazards.empty() &&
        selva::hazard::positionIsInAvoidedZone(actor.perception.last_known_player_pos,
                                               actor.archetype->avoids_hazards))
    {
        actor.intent_xz = glm::vec2(0.0f);
        return NodeResult::Success;
    }
    // Territory chase gate: drop chase when the player is in a region
    // foreign to this actor. The per-actor territory clamp in
    // tickPhysicsAndSyncActors is the navigation backstop -- a larva
    // chasing toward a player past a foreign boundary hits the
    // boundary, gets snapped back, slides along, and over successive
    // frames finds its way around.
    const std::string& player_region =
        engine::world::regionIdAtPosition(actor.perception.last_known_player_pos);
    const bool player_in_foreign = !player_region.empty() && player_region != actor.spawn_region_id;
    logTerritoryGateEdge(actor, player_region, player_in_foreign);
    if (player_in_foreign)
    {
        actor.intent_xz = glm::vec2(0.0f);
        return NodeResult::Success;
    }
    const float dx = actor.perception.last_known_player_pos.x - actor.pos.x;
    const float dz = actor.perception.last_known_player_pos.z - actor.pos.z;
    const float dist_sq = dx * dx + dz * dz;
    const float stop_range = computeStopRange(actor, tun);
    const float stop_sq = stop_range * stop_range;
    if (dist_sq > stop_sq && dist_sq > 1e-6f)
    {
        const float dist = std::sqrt(dist_sq);
        // Per-archetype chase speed in Combat (e.g. wolf gallops at
        // 6 m/s); otherwise fall back to walk_speed. Quadrupeds whose
        // locomotion IS their aggression need this; humanoid shades
        // leave chase_speed = 0 and stay at walking pace.
        const float speed = (actor.archetype != nullptr && actor.archetype->chase_speed > 0.0f &&
                             actor.perception.awareness >= Awareness::Combat)
                                ? actor.archetype->chase_speed
                                : tun.walk_speed;
        actor.intent_xz = glm::vec2(dx / dist, dz / dist) * speed;
    }
    else
    {
        actor.intent_xz = glm::vec2(0.0f);
    }
    return NodeResult::Success;
}

NodeResult IfAwarenessAtLeast::tick(Actor& actor, const selva::tuning::Tunables& /*tun*/)
{
    return (actor.perception.awareness >= min_required) ? NodeResult::Success : NodeResult::Failure;
}

NodeResult LeafPickAction::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    // No archetype = no actions to pick. (Test-dummy fallback path
    // where archetype binding wasn't found at spawn.)
    if (actor.archetype == nullptr || actor.archetype->actions.empty())
        return NodeResult::Failure;
    // Don't interrupt a swing in progress.
    if (actor.sampler.isOneShotActive())
        return NodeResult::Failure;
    const float now = selva::wallClock();
    // Freshness gate: don't fire actions on stale perception.
    // Without this, after knockdown (during which the prone actor's
    // vision cone pointed into the dirt and last_seen_time froze)
    // the actor would wake up and swing in the pre-knockdown
    // direction even if the player had moved. With the gate, the
    // Selector falls through to LeafMoveToTarget — the actor walks
    // toward last_known_player_pos, rotates as it walks, the cone
    // sweeps, perception re-acquires, then the swing fires legally.
    // -1 last_seen_time = never seen; treat as stale.
    if (actor.perception.last_seen_time < 0.0f ||
        (now - actor.perception.last_seen_time) > tun.ai_action_freshness_seconds)
        return NodeResult::Failure;
    const float dx = actor.perception.last_known_player_pos.x - actor.pos.x;
    const float dz = actor.perception.last_known_player_pos.z - actor.pos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    // Filter to legal actions, accumulate weights.
    struct Candidate
    {
        const EnemyAction* action;
        float cumulative_weight;
    };
    std::vector<Candidate> legal;
    legal.reserve(actor.archetype->actions.size());
    float total_weight = 0.0f;
    for (const auto& a : actor.archetype->actions)
    {
        if (!actionLegal(actor, a, dist, now))
            continue;
        total_weight += a.weight;
        legal.push_back({&a, total_weight});
    }
    if (legal.empty())
        return NodeResult::Failure;
    // Weighted-random roll. Per-actor RNG so two enemies of the same
    // archetype don't synchronize their picks.
    std::uniform_real_distribution<float> roll(0.0f, total_weight);
    const float r = roll(actor.rng);
    const EnemyAction* picked = legal.back().action; // fallback for fp edge
    for (const auto& c : legal)
    {
        if (r <= c.cumulative_weight)
        {
            picked = c.action;
            break;
        }
    }
    // Look up the action clip in the actor's OWN skeleton registry --
    // wolf actions reference clips in clipsByKey("wolf"), not the
    // player registry. Feeding a player clip (e.g. 65-track) to the
    // wolf sampler (53-joint) writes uninit SoaTransform lanes and
    // asserts at IsNormalizedEst. Same root cause as the engage-time
    // crash earlier this branch.
    const auto& reg = selva::anim::clipsByKey(actor.skeleton_id);
    const auto* clip = reg.get(picked->clip);
    if (clip == nullptr || !clip->isLoaded())
    {
        selva::combat::combatLog(
            "[ai-action] picked='{}' but clip '{}' not loaded on skeleton '{}'", picked->id,
            picked->clip, actor.skeleton_id);
        return NodeResult::Failure;
    }
    fireAction(actor, *picked, *clip, dist, now);
    return NodeResult::Success;
}

void BehaviorTree::tick(Actor& actor, const selva::tuning::Tunables& tun) const
{
    if (root)
        root->tick(actor, tun);
}

void BehaviorTreeRegistry::registerTree(std::string id, std::unique_ptr<BehaviorTree> tree)
{
    by_id[std::move(id)] = std::move(tree);
}

const BehaviorTree* BehaviorTreeRegistry::get(const std::string& id) const
{
    const auto it = by_id.find(id);
    return (it == by_id.end()) ? nullptr : it->second.get();
}

BehaviorTreeRegistry& behaviorTrees()
{
    static BehaviorTreeRegistry instance;
    return instance;
}

namespace
{

// Helper: build a Sequence node from a fixed-length initializer list
// of node pointers. Cleaner than constructing the vector inline at
// every tree-building site.
template <typename... Nodes> std::unique_ptr<Sequence> makeSequence(Nodes... nodes)
{
    std::vector<NodePtr> children;
    children.reserve(sizeof...(nodes));
    (children.emplace_back(std::move(nodes)), ...);
    return std::make_unique<Sequence>(std::move(children));
}

template <typename... Nodes> std::unique_ptr<Selector> makeSelector(Nodes... nodes)
{
    std::vector<NodePtr> children;
    children.reserve(sizeof...(nodes));
    (children.emplace_back(std::move(nodes)), ...);
    return std::make_unique<Selector>(std::move(children));
}

std::unique_ptr<BehaviorTree> buildHumanoidBasicTree()
{
    // Root selector tries each branch in priority order:
    //   Combat branch: inner selector — LeafPickAction first (fire
    //     a legal action if one is ready). If no action is legal
    //     (out of range, all on cooldown, or one-shot in flight),
    //     fall through to LeafMoveToTarget to close the gap. This
    //     is the Souls-feel core: action when ready, spacing when
    //     not. The Sprint 4a rule "Combat shares Alerted's
    //     locomotion" is preserved — Combat enemies follow the
    //     player out of melee instead of waiting for disengage.
    //   Alerted branch: walk toward target.
    //   Default: stand at spawn pose.
    // Combat-branch try order: fire an action (close enough + ready);
    // else circle-strafe (locked + inside duel range — Elden Ring
    // dance pattern); else close the gap.
    // Scripted target wins above Combat: a Scene-driven scripted walk
    // (Guide rescue, Patches-betrayal-style flee, etc.) overrides
    // whatever combat AI would otherwise do. The leaf returns Failure
    // when scripted_target_pos is NaN-sentinel, so the rest of the
    // tree runs normally for non-scripted actors.
    auto combat_branch = makeSequence(std::make_unique<IfAwarenessAtLeast>(Awareness::Combat),
                                      makeSelector(std::make_unique<LeafPickAction>(),
                                                   std::make_unique<LeafCircleTarget>(),
                                                   std::make_unique<LeafMoveToTarget>()));
    auto alerted_branch = makeSequence(std::make_unique<IfAwarenessAtLeast>(Awareness::Alerted),
                                       std::make_unique<LeafMoveToTarget>());
    auto root = makeSelector(std::make_unique<LeafFollowScriptedTarget>(), std::move(combat_branch),
                             std::move(alerted_branch), std::make_unique<LeafIdle>());
    return std::make_unique<BehaviorTree>(std::move(root));
}

} // namespace

void initBehaviorTrees()
{
    behaviorTrees().registerTree("humanoid_basic", buildHumanoidBasicTree());
}

} // namespace selva::gameplay
