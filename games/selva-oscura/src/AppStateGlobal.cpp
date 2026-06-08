#include "AppStateGlobal.h"

#include <algorithm>

namespace selva
{

GameState& gameState()
{
    static GameState s_gs;
    return s_gs;
}

SaveData& saveData()
{
    static SaveData s_sd;
    return s_sd;
}

UIState& uiState()
{
    static UIState s_ui;
    return s_ui;
}

Inventory& playerInventory()
{
    static Inventory s_inv;
    return s_inv;
}

Equipment& playerEquipment()
{
    static Equipment s_eq;
    return s_eq;
}

PlayerProfile* activePlayerProfile()
{
    // Unnamed runs are PlayerProfiles with name=="" living in
    // saveData like any other. active_character holds "" for the
    // unnamed case; the equality lookup just finds the empty-named
    // profile. No separate in-memory branch.
    const std::string& name = gameState().active_character;
    if (gameState().phase != GameState::Phase::Playing)
        return nullptr;
    for (auto& p : saveData().characters)
    {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

std::string activeCharacterDisplayName()
{
    const std::string& name = gameState().active_character;
    if (name.empty())
        return "???";
    return name;
}

bool hasFlag(const PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    return std::find(profile->flags.begin(), profile->flags.end(), flag) != profile->flags.end();
}

bool setFlag(PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    if (std::find(profile->flags.begin(), profile->flags.end(), flag) != profile->flags.end())
        return false;
    profile->flags.push_back(flag);
    return true;
}

bool clearFlag(PlayerProfile* profile, const std::string& flag)
{
    if (profile == nullptr || flag.empty())
        return false;
    auto it = std::find(profile->flags.begin(), profile->flags.end(), flag);
    if (it == profile->flags.end())
        return false;
    profile->flags.erase(it);
    return true;
}

bool hasFlag(const std::string& flag)
{
    return hasFlag(activePlayerProfile(), flag);
}

bool setFlag(const std::string& flag)
{
    return setFlag(activePlayerProfile(), flag);
}

bool clearFlag(const std::string& flag)
{
    return clearFlag(activePlayerProfile(), flag);
}

bool hasInsight(const PlayerProfile* profile, const std::string& node)
{
    if (profile == nullptr || node.empty())
        return false;
    const auto& v = profile->unlocked_insights;
    return std::find(v.begin(), v.end(), node) != v.end();
}

bool setInsight(PlayerProfile* profile, const std::string& node)
{
    if (profile == nullptr || node.empty())
        return false;
    auto& v = profile->unlocked_insights;
    if (std::find(v.begin(), v.end(), node) != v.end())
        return false;
    v.push_back(node);
    return true;
}

bool clearInsight(PlayerProfile* profile, const std::string& node)
{
    if (profile == nullptr || node.empty())
        return false;
    auto& v = profile->unlocked_insights;
    auto it = std::find(v.begin(), v.end(), node);
    if (it == v.end())
        return false;
    v.erase(it);
    return true;
}

bool hasInsight(const std::string& node)
{
    return hasInsight(activePlayerProfile(), node);
}

bool setInsight(const std::string& node)
{
    return setInsight(activePlayerProfile(), node);
}

bool clearInsight(const std::string& node)
{
    return clearInsight(activePlayerProfile(), node);
}

selva::dialog::NpcEncounterState& npcEncounter(PlayerProfile* profile, const std::string& npc_id)
{
    static selva::dialog::NpcEncounterState scratch;
    if (profile == nullptr || npc_id.empty())
    {
        scratch = selva::dialog::NpcEncounterState{};
        return scratch;
    }
    return profile->npc_state[npc_id];
}

bool hasSeenTopic(const PlayerProfile* profile, const std::string& npc_id,
                  const std::string& topic_id)
{
    if (profile == nullptr || npc_id.empty() || topic_id.empty())
        return false;
    const auto it = profile->npc_state.find(npc_id);
    if (it == profile->npc_state.end())
        return false;
    return it->second.topics_seen.count(topic_id) > 0;
}

bool hasSeenTopic(const std::string& npc_id, const std::string& topic_id)
{
    return hasSeenTopic(activePlayerProfile(), npc_id, topic_id);
}

} // namespace selva
