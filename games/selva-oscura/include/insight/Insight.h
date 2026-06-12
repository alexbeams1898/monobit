#pragma once

// Insight graph -- the rule layer of the insight system.
//
// Pillar (per docs/design/insight_revelation_system.md): the Vagrant's
// understanding of the world grows through play. Every player-visible
// string flows through the language map (selva::lang); each tiered
// entry can promote to higher tiers when a NAMED INSIGHT NODE has
// fired. This module is the rule engine that decides WHEN a node
// fires.
//
// Doctrine:
//   - Authoring is data-driven. Nodes live in config/insight/*.json.
//     Each node names ONE trigger (flag set, dialog began, examined a
//     mesh, etc.); when the trigger fires, the node's id is added to
//     the active profile's unlocked_insights set.
//   - Per-character storage. Each Vagrant starts fresh; no
//     cross-character carry. Reset happens via
//     hardResetWorldForCharacter -> selva::insight::reset().
//   - The language map (selva::lang) and the insight graph share ONE
//     node-id namespace. Renaming a node id breaks both saves AND
//     language-map authoring; treat names like a save schema field.
//
// v1 trigger kinds (apply to observation nodes only):
//   - "flag_set"             { flag: "..." }                fires when hasFlag()
//   - "dialog_began"         { npc_id: "..." }              fires from notifyDialogBegan()
//   - "examined"             { mesh_debug_name: "..." }     fires from notifyExamined()
//   - "kill_count"           { archetype: "...", threshold: N }   fires when
//   profile.kill_counts[archetype] >= N
//   - "sangue_accumulated"   { threshold: N }               fires when sangue_lifetime >= N
//
// Node kinds:
//   - "observation" (default) -- passive sensory facts the Vagrant
//     gathers by encountering the world. Fires via the trigger
//     mechanism above.
//   - "conclusion" -- inferential synthesis from observations. Does
//     NOT fire from a trigger; fires when the player explicitly
//     combines the right observations on the Mind sub-page. Carries
//     a "requires": [observation_ids] list naming the observations
//     that must be unlocked AND selected together to fire it.

#include <cstdint>
#include <string>
#include <vector>

namespace selva::insight
{

// Light categorization for the Mind sub-page (and other future
// surfaces that want to organize the insight graph). Each node
// declares a category in its JSON; the Mind page groups fired
// insights by category for display. Per the locked design (LOCKED
// 2026-06-09): start with three categories; add more only when a
// piece of content genuinely doesn't fit.
//
//   World   -- what the Vagrant has come to understand about the
//              place he's in (the pile, the larvae, the wood,
//              future encounters).
//   Self    -- what he's come to understand about his own form
//              (kills yield count, he holds substance, his body
//              changes when he commits, etc.).
//   Others  -- what he's come to understand about specific beings
//              he has met (the Guide, future NPCs, named keepers).
//
// Unknown is the parser fallback for nodes without a category
// field; the Mind page filters them out of the visible regions
// (logged as a noisy authoring miss).
enum class Category : std::uint8_t
{
    Unknown = 0,
    World = 1,
    Self = 2,
    Others = 3,
};

// Node kind -- observation (passive sensory fact) or conclusion
// (inferential synthesis). Observations fire from triggers; conclusions
// fire only via the player's explicit deduction on the Mind sub-page.
// Default is Observation; nodes without an explicit "kind" field load
// as observations.
enum class NodeKind : std::uint8_t
{
    Observation = 0,
    Inference = 1,
};

// Authored position on the Mind sub-page canvas. Coordinates are in
// canvas units (0..1 normalized to the canvas region). Nodes without
// an authored pos render at the origin -- visible as a layout miss in
// dev so the author notices.
struct NodePos
{
    float x = 0.0f;
    float y = 0.0f;
};

// Load every config/insight/*.json file into the in-memory node
// graph. Called once at boot, after selva::lang::loadDirectory.
// Idempotent: clears + reloads on each call.
void loadDirectory(const std::string& dir_path = "config/insight");

// Per-frame evaluator. Walks every loaded node; for each not-yet-
// unlocked node, evaluates its trigger against the active profile's
// state and fires (sets the node in unlocked_insights) if the trigger
// condition is met. Cheap: small N (a few dozen nodes max in v1) +
// per-node check is a flag/map lookup.
//
// Idempotent: nodes already unlocked are skipped.
void tick();

// Event-source hooks. Wired into the places where the underlying
// events happen (dialog::begin, text::beginExamine, fireEnemyDeath).
// These don't fire nodes directly -- they record event bookkeeping in
// the profile, and tick() walks the graph to fire nodes whose
// triggers reference that event-state. Decouples event-publishers
// from the graph-walker so nodes can be added/removed without
// touching the publisher sites.
//
// notifyDialogBegan: per-NPC encounter state (npc_state[id]) is
//   already updated by dialog::begin; this hook is a marker that the
//   event happened (insight tick reads encounter.times_talked).
// notifyExamined: similarly, set a per-mesh "examined" flag on the
//   profile via the existing flag system; trigger evaluator reads via
//   hasFlag("examined:<mesh_debug_name>").
// notifyKill: increment the profile's kill_counts map for the
//   killed archetype id.
void notifyDialogBegan(const std::string& npc_id);
void notifyExamined(const std::string& mesh_debug_name);
void notifyKill(const std::string& archetype_id);

// Current examine count for a subject (per active profile). Returns
// 0 if never examined. Read by examine on_interact closures BEFORE
// they call notifyExamined, so the text tier matches the count the
// player is about to land on.
std::uint32_t examineCountOf(const std::string& subject);

// Reset the in-memory graph (test seam + dev reload path). Note:
// this does NOT clear the active profile's unlocked_insights --
// that's per-character state, owned by the profile. Use
// hardResetWorldForCharacter to wipe profile state.
void resetGraph();

// Query the category for a given node id (from the loaded graph,
// not the player's unlocked set). Returns Category::Unknown if the
// node id isn't loaded.
Category categoryOf(const std::string& node_id);

// Return the list of node ids the active profile has unlocked that
// belong to the given category. Order is insertion order from the
// graph load -- stable across launches if the JSON files don't
// reorder. Used by the Mind sub-page to render fired insights
// grouped by category.
std::vector<std::string> firedInsightsInCategory(Category cat);

// Query the kind of a given node id (from the loaded graph). Returns
// NodeKind::Observation if the node id isn't loaded (matches the
// default-on-absent behavior for the field).
NodeKind kindOf(const std::string& node_id);

// Source identifier for clustering -- a string two observations
// share iff they came from the same in-world subject:
//   - examined trigger  : the examine subject string (e.g. "dead_lonza")
//   - dialog_began      : the npc id (e.g. "guide")
//   - kill_count        : the archetype id (e.g. "larva_aged")
//   - flag_set          : empty (these are mechanical triggers with no
//                         shared subject by default)
//   - sangue_accumulated: empty
// Returns empty for ids not loaded and for conclusions (conclusions
// aren't sources; they're synthesis points).
std::string sourceOf(const std::string& node_id);

// Authored canvas position for the node. Returns {0,0} for unknown
// node ids and for nodes without an authored pos (visible as a layout
// miss in dev). UI is the only consumer.
NodePos posOf(const std::string& node_id);

// The list of observation node ids a conclusion requires. Empty for
// observations and for unknown ids. UI reads this to draw edges
// from each conclusion back to its supporting observations.
std::vector<std::string> requiresOf(const std::string& node_id);

// Per cognition-system v1 (simplified): an inference's authored
// readings. Each reading is just an id + the Vagrant's interior
// voice for that interpretation. Warrant is now a property of the
// inference itself (non-empty linked_observations), not per-reading.
struct ReadingSpec
{
    std::string id;
    std::string text;
};
std::vector<ReadingSpec> readingsOf(const std::string& node_id);

// The list of observation node ids that CONFIRM a conclusion --
// upgrade it from uncertain (deduced) to certain. Empty for
// observations and for conclusions without a confirmation path
// (those conclusions fire directly at certain when deduced; absence
// of confirmed_by means there's nothing to confirm). UI draws
// confirming-edges in a distinct style from deduction-edges.
std::vector<std::string> confirmedByOf(const std::string& node_id);

// True if the given (already-unlocked) conclusion is in the certain
// state. False for observations, for unlocked-but-uncertain
// conclusions, and for not-yet-unlocked ids. Read by the Mind canvas
// to pick the certain vs uncertain visual treatment + by the lang
// resolver to tier-promote the conclusion's summary.
bool isCertain(const std::string& node_id);

// Return every node id the active profile has unlocked, regardless
// of category. Used by the Mind sub-page canvas which displays the
// full graph spatially (no category grouping). Order is insertion
// order from the graph load.
std::vector<std::string> firedInsights();

// Attempt to fire a conclusion node from the given set of observation
// node ids. Returns the node id of the conclusion that fires, or an
// empty string if no conclusion's `requires` list exactly matches the
// given observation set. Player-driven deduction entry point: the
// Mind sub-page selection action calls this with the player's selected
// observation ids; on a hit, the conclusion is added to the profile's
// unlocked_insights set and surfaces on the page next frame.
//
// Match rule: the provided observation set must equal the conclusion's
// `requires` set (same ids, same count). Order does not matter. If the
// player has not actually unlocked all of the requested observations,
// the call is rejected (returns empty string) -- you cannot "deduce
// from" observations you haven't earned.
// Two-phase Deduce: caller first asks which inference would match
// their selected observations (matchInference), gets the inference
// id + that inference's authored readings via readingsOf, displays
// a picker, then calls commitDeduce with the chosen reading id.
//
// matchInference returns the inference id whose `requires` is a
// subset of the selected set (set-equality with the smallest match
// per cognition-system v1). Empty string when no match.
std::string matchInference(const std::vector<std::string>& selected_observation_ids);

// Fires the inference id (or returns it if already unlocked) so the
// caller can place it on the workbench. Caller owns the reading id
// pick and stores it in their workbench node. Empty string return
// means the inference wasn't found.
std::string commitDeduce(const std::string& inference_id);

// Back-compat shim for any caller still using the prior single-phase
// API. Returns the matched inference id (already-unlocked or freshly
// fired). Does NOT pick a reading; the caller is responsible for
// running the reading picker via readingsOf + commitDeduce.
std::string tryDeduce(const std::vector<std::string>& selected_observation_ids);

// Player-driven certainty: link an already-unlocked observation to an
// already-unlocked uncertain conclusion as its confirming evidence.
// Returns true and promotes the conclusion to certain when:
//   - both ids are unlocked on the active profile,
//   - the conclusion is in fact a conclusion with a non-empty
//     confirmed_by list,
//   - the observation id appears in that list,
//   - the conclusion is not already certain.
// Otherwise returns false and changes nothing. Recomputes layout on
// success so the confirming observation snaps next to the conclusion.
bool tryConfirm(const std::string& conclusion_id, const std::string& observation_id);

} // namespace selva::insight
