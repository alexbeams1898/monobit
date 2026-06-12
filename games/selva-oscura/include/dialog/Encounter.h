#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace selva::dialog
{

// Per-character record of one NPC the player has interacted with.
// Sparse: only NPCs the player has ever talked to appear in
// PlayerProfile.npc_state. Topic ids in `topics_seen` are stable
// identifiers (see config/npcs/<id>.json -- treat as save-schema).
struct NpcEncounterState
{
    int times_talked = 0;
    std::unordered_set<std::string> topics_seen;
};

} // namespace selva::dialog
