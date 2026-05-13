#pragma once

#include "gameplay/Perception.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::tuning
{
struct Tunables;
}

namespace selva::gameplay
{

struct Actor;

// Return value from a behavior-tree node tick. The semantics mirror
// the standard BT convention used by Unreal's BTService, Unity
// Behavior Designer, and FromSoft's mob-tier enemies.
//
//   Success — this node handled the tick; parent should consider its
//             work done. Selector children: stop iterating and return
//             Success up. Sequence children: continue to next child.
//   Failure — this node could not handle the tick; parent considers
//             this child a no-op. Selector: try next child. Sequence:
//             abort the sequence and propagate Failure.
//
// Running is not modeled in v1 — every leaf is "instant" from the
// tree's perspective (it writes intent + maybe fires a one-shot,
// then returns Success). Long-running behaviors (a swing in progress)
// are tracked via cooldowns + Actor.sampler state, NOT via tree
// state. This keeps the tree stateless and re-entrant per tick,
// which makes the AI scheduler's "fire the tree every decision tick"
// model clean.
enum class NodeResult
{
    Success,
    Failure,
};

// Polymorphic node base. tick() is the only virtual; derived types
// own their children (Selector / Sequence) and whatever per-node
// data they need.
class Node
{
  public:
    virtual ~Node() = default;
    virtual NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) = 0;
};

using NodePtr = std::unique_ptr<Node>;

// Selector — "try children in order, succeed if any succeed."
// Returns Success on the first child that returns Success; Failure
// only if every child failed. Equivalent to a priority-ordered
// if/else chain.
class Selector : public Node
{
  public:
    explicit Selector(std::vector<NodePtr> children) : children_(std::move(children))
    {
    }
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;

  private:
    std::vector<NodePtr> children_;
};

// Sequence — "run all children in order, fail if any fail."
// Returns Success only if every child returned Success; Failure on
// the first child that failed. Equivalent to a guarded-AND chain.
class Sequence : public Node
{
  public:
    explicit Sequence(std::vector<NodePtr> children) : children_(std::move(children))
    {
    }
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;

  private:
    std::vector<NodePtr> children_;
};

// Leaf — actor has no current awareness of the player. Write
// no intent (preserve previous frame's), hold spawn yaw. Always
// returns Success.
class LeafIdle : public Node
{
  public:
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;
};

// Leaf — face the actor's last_known_player_pos, zero intent_xz.
// Used during Combat-on-cooldown and Suspicious. Always returns
// Success.
class LeafIdleFace : public Node
{
  public:
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;
};

// Leaf — walk toward last_known_player_pos at walk_speed until
// distance < ai_combat_engage_range_meters; then zero intent and
// hold. Always returns Success.
class LeafMoveToTarget : public Node
{
  public:
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;
};

// Condition leaf — Succeeds if actor.perception.awareness >=
// `min`. Fails otherwise. Composed with Sequence to gate sub-trees
// on awareness level.
class IfAwarenessAtLeast : public Node
{
  public:
    explicit IfAwarenessAtLeast(Awareness min) : min_(min)
    {
    }
    NodeResult tick(Actor& actor, const selva::tuning::Tunables& tun) override;

  private:
    Awareness min_;
};

// Tree wrapper — owns the root and exposes a single tick() entry
// point. Trees are stateless and shareable across actors (state lives
// on Actor); a single instance of each tree is built once at startup.
class BehaviorTree
{
  public:
    explicit BehaviorTree(NodePtr root) : root_(std::move(root))
    {
    }
    void tick(Actor& actor, const selva::tuning::Tunables& tun) const;

  private:
    NodePtr root_;
};

// Process-wide registry of constructed trees keyed by tree_id. Built
// once at startup via initBehaviorTrees(); read by gameplay code via
// behaviorTrees().get(tree_id). Pattern matches selva::anim::clips()
// and selva::gameplay::archetypes().
class BehaviorTreeRegistry
{
  public:
    void registerTree(std::string id, std::unique_ptr<BehaviorTree> tree);
    const BehaviorTree* get(const std::string& id) const;
    const std::unordered_map<std::string, std::unique_ptr<BehaviorTree>>& all() const
    {
        return by_id_;
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<BehaviorTree>> by_id_;
};

BehaviorTreeRegistry& behaviorTrees();

// Construct the standard humanoid behavior tree and register it
// under "humanoid_basic". Called once at startup. The tree shape:
//
//   Selector (root)
//   ├── Sequence [IfAwarenessAtLeast(Combat),
//   │            Selector [ /* commit 2: LeafPickAction here */
//   │                       LeafMoveToTarget ]]
//   ├── Sequence [IfAwarenessAtLeast(Alerted), LeafMoveToTarget]
//   └── LeafIdle
//
// Sprint 4b commit 1: no action firing yet. The Combat branch falls
// through to LeafMoveToTarget — matches Alerted's locomotion so
// Combat enemies follow the player out of melee instead of standing
// still. Commit 2 inserts LeafPickAction (swing if in melee +
// cooldown ready) ahead of LeafMoveToTarget in the Combat-branch's
// inner Selector.
void initBehaviorTrees();

} // namespace selva::gameplay
