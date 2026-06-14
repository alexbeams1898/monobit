#include "AppStateGlobal.h"

#include "gameplay/Actor.h"
#include "lang/Language.h"
#include "notice/Notices.h"
#include "ui/Notifications.h"

#include <algorithm>
#include <cmath>
#include <string>

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
    // Forward to the active profile's inventory so the UI sees the
    // same items the SaveManager persists. Falls back to a static
    // empty inventory when no active profile (e.g. at MainMenu).
    static Inventory s_empty;
    PlayerProfile* p = activePlayerProfile();
    return (p != nullptr) ? p->inventory : s_empty;
}

Equipment& playerEquipment()
{
    // Same active-profile forwarding as playerInventory().
    static Equipment s_empty;
    PlayerProfile* p = activePlayerProfile();
    return (p != nullptr) ? p->equipment : s_empty;
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

    // Persistent unread marker for the Mind sub-page node badge.
    // Shares storage with item / topic / recipe / future-domain
    // unread state via the cross-domain notice module so we don't
    // grow parallel per-domain fields. Acknowledged when the
    // player clicks the node on the Mind sub-page (UI wires the
    // acknowledge call). setInsight is also called from
    // SaveManager-adjacent restore paths -- those go through
    // unlocked_insights directly (NOT through this function), so
    // save-load doesn't re-mark notices.
    selva::notice::mark(selva::notice::kDomainInsight, node);

    // Bottom-right toast on first-time unlock. Per [[project_cognition_system_v1]]
    // this is an "observation" event -- the cognition system registered
    // something new. Per-insight override via lang key
    // `notif.<node>.title`; default is the generic "New observation."
    // Cool cyan palette distinguishes cognitive events from item-pickup
    // gold/neutral.
    const std::string key = "notif." + node + ".title";
    const std::string& resolved = selva::lang::resolve(key);
    // resolve() returns "[lang:KEY]" on missing -- detect via prefix and
    // fall back to the generic default. Authors can layer in per-insight
    // overrides later by adding the lang key.
    const bool has_override = resolved.rfind("[lang:", 0) != 0;
    // Match the item toast format (+N <thing>) so the visual
    // grammar is consistent across discovery types. Per-insight
    // overrides supply their full string verbatim (the override is
    // assumed to be authored as the complete toast); only the
    // default uses the "+1" prefix.
    const std::string toast = has_override ? resolved : std::string("+1 New Observation");
    const glm::vec4 cyan{0.55f, 0.85f, 0.95f, 1.0f};
    selva::ui::pushNotification(toast, cyan);

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

int computeCognitiveStat(std::uint32_t growth)
{
    // 1 + floor(log2(growth + 1)). 0 events -> 1, 1 -> 2, 3 -> 3, 7 -> 4,
    // 15 -> 5, 31 -> 6, 63 -> 7, etc. Caps practically around 12-15
    // for late-game grinders.
    if (growth == 0)
        return 1;
    int s = 1;
    std::uint32_t v = growth + 1;
    while (v > 1)
    {
        v >>= 1;
        ++s;
    }
    return s;
}

namespace
{
void syncCognitiveStatsToActor()
{
    PlayerProfile* p = activePlayerProfile();
    if (p == nullptr)
        return;
    auto& a = selva::gameplay::player();
    a.stats.per = computeCognitiveStat(p->perception_growth);
    a.stats.cog = computeCognitiveStat(p->cognition_growth);
    a.stats.intl = computeCognitiveStat(p->intelligence_growth);
}
} // namespace

void growPerception(std::uint32_t amount)
{
    PlayerProfile* p = activePlayerProfile();
    if (p == nullptr)
        return;
    p->perception_growth += amount;
    syncCognitiveStatsToActor();
}

void growCognition(std::uint32_t amount)
{
    PlayerProfile* p = activePlayerProfile();
    if (p == nullptr)
        return;
    p->cognition_growth += amount;
    syncCognitiveStatsToActor();
}

void growIntelligence(std::uint32_t amount)
{
    PlayerProfile* p = activePlayerProfile();
    if (p == nullptr)
        return;
    p->intelligence_growth += amount;
    syncCognitiveStatsToActor();
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
