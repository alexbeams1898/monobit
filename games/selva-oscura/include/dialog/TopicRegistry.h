#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::dialog
{

// Conditions on topic visibility / choice availability. Flag-based.
// `flags_required` -- ALL must be set on PlayerProfile (or be a
// synthesized "seen_topic_X" flag for this NPC). `flags_forbidden`
// -- NONE may be set. `custom` -- optional handler key looked up in
// the condition-handler registry (returns bool). Empty struct = no
// gate (always available).
struct ShowWhen
{
    std::vector<std::string> flags_required;
    std::vector<std::string> flags_forbidden;
    std::string custom;
};

// One choice the player can pick on a given topic. label is the
// player-facing modern-register text. next_topic is the id of the
// topic to transition to, OR a sentinel: "__end__" exits the
// dialog, "__same__" re-displays the current topic.
struct Choice
{
    std::string id;
    std::string label;
    std::string next_topic;
    ShowWhen choice_show_when;
    std::string on_select;
};

// One topic = NPC speech + optional choices + optional handlers.
struct Topic
{
    std::string id;
    std::string speaker; // NPC id, or a sentinel ("narration", "player")
    std::string line;
    ShowWhen show_when;
    std::vector<Choice> choices;
    std::string on_enter;
    std::string on_exit;
    // Insight node id to fire when the player enters this topic.
    // Empty = no insight fire. Used by the "named-by-Guide" pattern:
    // when the Guide tells the Vagrant about a thing the player has
    // examined, this field names the insight node that promotes the
    // language map entries elsewhere (the named thing's display name,
    // its examine prose). Per the locked design: ENTIRE TOPIC fires
    // the insight (not per-choice). One-shot reveals.
    std::string unlocks_insight;
    // True if this topic is a valid conversation-start entry point
    // (something dialog::begin() can land on by picking the first
    // matching topic). False for internal transition topics that are
    // only reachable via another topic's choice.next_topic. Default
    // false -- topics opt IN to being entry-able. Without this,
    // every transition topic with no show_when would silently become
    // a fallback entry point.
    bool entry_point = false;
};

// All topics for one NPC. Loaded from config/npcs/<npc_id>.json.
struct NpcDialog
{
    std::string npc_id;
    std::string display_name;
    // Language-map key for the speaker name shown in the dialog
    // header. Tier-gated through the insight system; tier-0 is "???"
    // until the player gains the insight that knows this NPC. When
    // empty, the dialog falls back to display_name (literal).
    std::string display_name_key;
    std::vector<Topic> topics;
};

class TopicRegistry
{
  public:
    // Load every *.json in `dir` as an NpcDialog. Files whose JSON
    // parse fails OR whose topics reference unknown next_topic ids
    // are logged and skipped (loud failure at boot, never at
    // runtime). next_topic sentinels (__end__ / __same__) are valid
    // without referencing a real topic.
    void loadDirectory(const std::filesystem::path& dir);

    // nullptr if no NPC with that id is loaded.
    const NpcDialog* get(const std::string& npc_id) const;

    // Lookup a specific topic within an NPC. nullptr if either id
    // doesn't resolve.
    const Topic* getTopic(const std::string& npc_id, const std::string& topic_id) const;

    const std::unordered_map<std::string, NpcDialog>& all() const
    {
        return by_id;
    }

  private:
    std::unordered_map<std::string, NpcDialog> by_id;
};

TopicRegistry& topicRegistry();

} // namespace selva::dialog
