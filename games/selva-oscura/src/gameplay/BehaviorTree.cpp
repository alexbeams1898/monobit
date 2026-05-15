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
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"

#include <cmath>
#include <random>
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
    if (a.range_max > 0.0f && dist_to_target > a.range_max)
        return false;
    if (a.weight <= 0.0f)
        return false;
    const auto it = actor.action_state.find(a.id);
    return it == actor.action_state.end() || now >= it->second.cooldown_until_time;
}

// Fire the picked action's one-shot + spawn its hitbox if declared.
// Returns the spawned hitbox id (0 if no hitbox). Logs the fire.
std::uint32_t fireAction(Actor& actor, const EnemyAction& picked,
                         const selva::anim::AnimationClip& clip, float dist_to_target, float now)
{
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = picked.clip.c_str();
    opts.freeze_last = picked.freeze_last;
    actor.sampler.playOneShot(clip, picked.blend_in_seconds, picked.blend_out_seconds,
                              selva::anim::PoseSampler::BodyMask::Full,
                              /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
    actor.action_state[picked.id].cooldown_until_time = now + picked.cooldown_seconds;
    std::uint32_t hitbox_id = 0;
    if (!picked.hitbox_joint.empty())
    {
        selva::combat::AttackHitboxSpawnParams sp;
        sp.actor = &actor;
        sp.attacker = selva::combat::OwnerRef{selva::combat::OwnerKind::Enemy, enemyIndex(actor)};
        sp.attacker_faction = actor.faction;
        sp.raw_damage = picked.raw_damage;
        sp.poise_damage = picked.poise_damage;
        sp.joint_name = picked.hitbox_joint.c_str();
        sp.hitbox_radius = picked.hitbox_radius;
        sp.hitbox_tip_offset_z = picked.hitbox_tip_offset_z;
        sp.clip_duration_seconds = clip.duration();
        sp.clip_start_seconds = 0.0f;
        sp.playback_rate = 1.0f;
        sp.mesh_foot_offset_y = selva::anim::playerMesh().foot_offset_y;
        hitbox_id = selva::combat::spawnAttackHitbox(sp);
    }
    selva::combat::combatLog(
        "[ai-action] actor picked=%s clip=%s dist=%.2f cd_until=%.3f hitbox_id=%u\n",
        picked.id.c_str(), picked.clip.c_str(), dist_to_target,
        actor.action_state[picked.id].cooldown_until_time, hitbox_id);
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
        if (a.range_max <= 0.0f) // no upper bound = unconstrained
            continue;
        if (closest < 0.0f || a.range_max < closest)
            closest = a.range_max;
    }
    return (closest > 0.0f) ? closest : tun.ai_combat_engage_range_meters;
}

} // namespace

NodeResult Selector::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    for (auto& child : children_)
    {
        if (child->tick(actor, tun) == NodeResult::Success)
            return NodeResult::Success;
    }
    return NodeResult::Failure;
}

NodeResult Sequence::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    for (auto& child : children_)
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

NodeResult LeafIdleFace::tick(Actor& actor, const selva::tuning::Tunables& /*tun*/)
{
    actor.intent_xz = glm::vec2(0.0f);
    actor.turn_intent_yaw = yawFacing(actor.pos, actor.perception.last_known_player_pos);
    return NodeResult::Success;
}

NodeResult LeafMoveToTarget::tick(Actor& actor, const selva::tuning::Tunables& tun)
{
    actor.turn_intent_yaw = yawFacing(actor.pos, actor.perception.last_known_player_pos);
    const float dx = actor.perception.last_known_player_pos.x - actor.pos.x;
    const float dz = actor.perception.last_known_player_pos.z - actor.pos.z;
    const float dist_sq = dx * dx + dz * dz;
    const float stop_range = computeStopRange(actor, tun);
    const float stop_sq = stop_range * stop_range;
    if (dist_sq > stop_sq && dist_sq > 1e-6f)
    {
        const float dist = std::sqrt(dist_sq);
        actor.intent_xz = glm::vec2(dx / dist, dz / dist) * tun.walk_speed;
    }
    else
    {
        actor.intent_xz = glm::vec2(0.0f);
    }
    return NodeResult::Success;
}

NodeResult IfAwarenessAtLeast::tick(Actor& actor, const selva::tuning::Tunables& /*tun*/)
{
    return (actor.perception.awareness >= min_) ? NodeResult::Success : NodeResult::Failure;
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
    const auto* clip = selva::anim::clips().get(picked->clip);
    if (clip == nullptr || !clip->isLoaded())
    {
        selva::combat::combatLog("[ai-action] picked='%s' but clip '%s' not loaded\n",
                                 picked->id.c_str(), picked->clip.c_str());
        return NodeResult::Failure;
    }
    fireAction(actor, *picked, *clip, dist, now);
    return NodeResult::Success;
}

void BehaviorTree::tick(Actor& actor, const selva::tuning::Tunables& tun) const
{
    if (root_)
        root_->tick(actor, tun);
}

void BehaviorTreeRegistry::registerTree(std::string id, std::unique_ptr<BehaviorTree> tree)
{
    by_id_[std::move(id)] = std::move(tree);
}

const BehaviorTree* BehaviorTreeRegistry::get(const std::string& id) const
{
    const auto it = by_id_.find(id);
    return (it == by_id_.end()) ? nullptr : it->second.get();
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
    auto combat_branch = makeSequence(
        std::make_unique<IfAwarenessAtLeast>(Awareness::Combat),
        makeSelector(std::make_unique<LeafPickAction>(), std::make_unique<LeafMoveToTarget>()));
    auto alerted_branch = makeSequence(std::make_unique<IfAwarenessAtLeast>(Awareness::Alerted),
                                       std::make_unique<LeafMoveToTarget>());
    auto root = makeSelector(std::move(combat_branch), std::move(alerted_branch),
                             std::make_unique<LeafIdle>());
    return std::make_unique<BehaviorTree>(std::move(root));
}

} // namespace

void initBehaviorTrees()
{
    behaviorTrees().registerTree("humanoid_basic", buildHumanoidBasicTree());
}

} // namespace selva::gameplay
