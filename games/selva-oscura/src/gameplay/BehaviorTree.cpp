#include "gameplay/BehaviorTree.h"

#include "Tunables.h"
#include "gameplay/Actor.h"

#include <cmath>

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
    const float stop_sq = tun.ai_combat_engage_range_meters * tun.ai_combat_engage_range_meters;
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
    //   Combat branch: inner selector — commit 2 adds
    //     LeafPickAction (swing if in melee + cooldown ready) ahead
    //     of LeafMoveToTarget so the swing has priority. Today
    //     (commit 1) Combat just falls through to LeafMoveToTarget,
    //     matching Alerted's locomotion. This preserves the Sprint
    //     4a rule "Combat shares Alerted's locomotion" — Combat
    //     enemies follow the player out of melee rather than
    //     standing still waiting for disengage.
    //   Alerted branch: walk toward target.
    //   Default: stand at spawn pose.
    auto combat_branch = makeSequence(std::make_unique<IfAwarenessAtLeast>(Awareness::Combat),
                                      makeSelector(std::make_unique<LeafMoveToTarget>()));
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
