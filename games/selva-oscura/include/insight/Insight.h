#pragma once

// The insight graph: the rule layer that decides when a named node fires.
// Nodes are authored in config/insight/*.json, each naming one trigger; a
// fired node's id joins the active profile's unlocked set, which is what lets
// the language map promote a string to a higher tier.
// See docs/design/insight_revelation_system.md.
//
// The language map and this graph share ONE node-id namespace. A renamed id
// breaks saved profiles and language authoring at once -- treat an id like a
// save-schema field, not a label.
//
// Storage is per character. Nothing carries across; reset runs through
// hardResetWorldForCharacter.

#include <cstdint>
#include <string>
#include <vector>

namespace selva::insight
{

// How the Mind page groups what has fired. Unknown is the parser's fallback
// for a node whose JSON omits the field, and the page filters those out --
// an authoring miss should disappear from the fiction, not decorate it.
enum class Category : std::uint8_t
{
    Unknown = 0,
    World = 1,
    Self = 2,
    Others = 3,
};

// An observation is gathered by meeting the world and fires from a trigger.
// An inference is reached by the player putting observations together, and
// never fires on its own. Absent in JSON means Observation.
enum class NodeKind : std::uint8_t
{
    Observation = 0,
    Inference = 1,
};

// Authored canvas position, normalised 0..1. An unauthored node lands at the
// origin, where a layout miss is visible rather than merely wrong.
struct NodePos
{
    float x = 0.0f;
    float y = 0.0f;
};

// Load config/insight/*.json. Once at boot, after the language map.
// Idempotent, clearing and reloading.
void loadDirectory(const std::string& dir_path = "config/insight");

// Evaluates every not-yet-unlocked observation's trigger against profile
// state and fires what matches. Idempotent; inferences are skipped, having
// no trigger to evaluate.
void tick();

// Event hooks, wired where the events actually happen. They record bookkeeping
// on the profile and fire nothing themselves -- tick() walks the graph. That
// indirection is what lets nodes be added or removed without editing any of
// the publisher sites.
void notifyDialogBegan(const std::string& npc_id);
void notifyExamined(const std::string& mesh_debug_name);
void notifyKill(const std::string& archetype_id);

// Times a subject has been examined, 0 if never. Read by examine closures
// BEFORE they call notifyExamined, so the text tier matches the count the
// player is about to land on rather than the one after it.
std::uint32_t examineCountOf(const std::string& subject);

// Drop the loaded graph. Test seam and dev reload; does NOT touch the
// profile's unlocked set, which is per-character state the profile owns.
void resetGraph();

// Queries against the loaded graph, not the player's unlocked set. Each
// returns its type's default for an id that was never loaded.
Category categoryOf(const std::string& node_id);
NodeKind kindOf(const std::string& node_id);
NodePos posOf(const std::string& node_id);

// A string two observations share when they came from the same in-world
// subject: the examine subject, the npc id, or the archetype killed. Empty for
// mechanical triggers, which have no shared subject, and for inferences, which
// are synthesis points rather than sources.
std::string sourceOf(const std::string& node_id);

// The observations an inference is built from, and the ones that can confirm
// it afterwards. Both empty for observations. An inference with nothing in
// confirmedByOf fires certain on the spot -- there is nothing left to confirm.
std::vector<std::string> requiresOf(const std::string& node_id);
std::vector<std::string> confirmedByOf(const std::string& node_id);

// An authored interpretation of an inference, in the Vagrant's interior voice.
struct ReadingSpec
{
    std::string id;
    std::string text;
};
std::vector<ReadingSpec> readingsOf(const std::string& node_id);

// Whether an unlocked inference has reached certainty. False for observations
// and for anything not yet unlocked.
bool isCertain(const std::string& node_id);

// Every node id the profile has unlocked, in graph load order -- stable across
// launches as long as the JSON does not reorder.
std::vector<std::string> firedInsights();

// Deduction runs in two phases so the player can choose a reading in between.
// matchInference names the inference the selection satisfies, or returns empty;
// commitDeduce fires it and hands back its id. Observations the player has not
// actually unlocked never match -- nothing is deduced from what was not earned.
std::string matchInference(const std::vector<std::string>& selected_observation_ids);
std::string commitDeduce(const std::string& inference_id);

// Link an unlocked observation to an unlocked uncertain inference as its
// confirming evidence, promoting it to certain. False, changing nothing, if
// either is still locked, if the observation is not on the inference's
// confirming list, or if it was already certain.
bool tryConfirm(const std::string& conclusion_id, const std::string& observation_id);

} // namespace selva::insight
