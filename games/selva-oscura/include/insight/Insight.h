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
// v1 trigger kinds:
//   - "flag_set"             { flag: "..." }                fires when hasFlag()
//   - "dialog_began"         { npc_id: "..." }              fires from notifyDialogBegan()
//   - "examined"             { mesh_debug_name: "..." }     fires from notifyExamined()
//   - "kill_count"           { archetype: "...", threshold: N }   fires when
//   profile.kill_counts[archetype] >= N
//   - "sangue_accumulated"   { threshold: N }               fires when sangue_lifetime >= N

#include <string>

namespace selva::insight
{

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

// Reset the in-memory graph (test seam + dev reload path). Note:
// this does NOT clear the active profile's unlocked_insights --
// that's per-character state, owned by the profile. Use
// hardResetWorldForCharacter to wipe profile state.
void resetGraph();

} // namespace selva::insight
