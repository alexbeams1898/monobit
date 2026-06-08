#include "SaveManager.h"

#include <nlohmann/json.hpp>

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace selva::SaveManager
{

// ---------------------------------------------------------------------------
// Save directory resolution
// ---------------------------------------------------------------------------

std::string getSaveDir()
{
    static std::string cached;
    if (!cached.empty())
        return cached;

    char* pref = SDL_GetPrefPath("SelvaOscura", "SelvaOscura");
    if (pref != nullptr)
    {
        cached = pref;
        SDL_free(pref);
    }
    else
    {
        cached = "saves/";
    }
    return cached;
}

std::string defaultSavePath()
{
    return getSaveDir() + "save.json";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace
{
void loadFelledBosses(const json& c, PlayerProfile& p)
{
    if (!c.contains("felled_bosses") || !c["felled_bosses"].is_array())
        return;
    for (const auto& b : c["felled_bosses"])
        if (b.is_string())
            p.felled_bosses.push_back(b.get<std::string>());
}

void loadFlags(const json& c, PlayerProfile& p)
{
    if (!c.contains("flags") || !c["flags"].is_array())
        return;
    for (const auto& f : c["flags"])
        if (f.is_string())
            p.flags.push_back(f.get<std::string>());
}

void loadInsights(const json& c, PlayerProfile& p)
{
    if (c.contains("unlocked_insights") && c["unlocked_insights"].is_array())
    {
        for (const auto& n : c["unlocked_insights"])
            if (n.is_string())
                p.unlocked_insights.push_back(n.get<std::string>());
    }
    if (c.contains("kill_counts") && c["kill_counts"].is_object())
    {
        for (auto it = c["kill_counts"].begin(); it != c["kill_counts"].end(); ++it)
            if (it.value().is_number_unsigned())
                p.kill_counts[it.key()] = it.value().get<std::uint32_t>();
    }
}

void loadDoorStates(const json& c, PlayerProfile& p)
{
    if (!c.contains("door_states") || !c["door_states"].is_array())
        return;
    for (const auto& entry : c["door_states"])
    {
        if (entry.is_array() && entry.size() == 2 && entry[0].is_string() && entry[1].is_string())
        {
            p.door_states.emplace_back(entry[0].get<std::string>(), entry[1].get<std::string>());
        }
    }
}

void loadInventoryEntry(const json& e, std::vector<selva::items::Entry>& vec)
{
    if (!e.is_object() || !e.contains("kind") || !e.contains("id"))
        return;
    const std::string kind = e.value("kind", std::string{});
    const std::string id = e.value("id", std::string{});
    if (kind == "possession")
        vec.emplace_back(selva::items::PossessionEntry{id});
    else if (kind == "stack")
        vec.emplace_back(selva::items::StackEntry{id, e.value("count", 0)});
    else if (kind == "instanced")
        vec.emplace_back(selva::items::InstancedEntry{id, e.value("upgrade_level", 0)});
}

void loadInventory(const json& c, PlayerProfile& p)
{
    if (!c.contains("inventory") || !c["inventory"].is_object())
        return;
    for (const auto& [cat_id, entries] : c["inventory"].items())
    {
        if (!entries.is_array())
            continue;
        auto& vec = p.inventory.by_category[cat_id];
        for (const auto& e : entries)
            loadInventoryEntry(e, vec);
    }
}

void loadNpcState(const json& c, PlayerProfile& p)
{
    if (!c.contains("npc_state") || !c["npc_state"].is_object())
        return;
    for (const auto& [npc_id, rec] : c["npc_state"].items())
    {
        if (!rec.is_object())
            continue;
        auto& state = p.npc_state[npc_id];
        state.times_talked = rec.value("times_talked", 0);
        if (rec.contains("topics_seen") && rec["topics_seen"].is_array())
            for (const auto& t : rec["topics_seen"])
                if (t.is_string())
                    state.topics_seen.insert(t.get<std::string>());
    }
}

PlayerProfile loadCharacter(const json& c)
{
    PlayerProfile p;
    p.name = c.value("name", std::string{});
    p.pos_x = c.value("pos_x", 0.0f);
    p.pos_y = c.value("pos_y", 0.0f);
    p.pos_z = c.value("pos_z", 0.0f);
    p.yaw = c.value("yaw", 0.0f);
    p.has_saved_pose = c.value("has_saved_pose", false);
    // Back-compat: legacy region ids (chapel_interior, acheron) all
    // collapsed into "surface" before the Scenes-to-Regions rename.
    p.current_region_id = c.value("current_region_id", std::string{"surface"});
    if (p.current_region_id == "chapel_interior" || p.current_region_id == "acheron")
        p.current_region_id = "surface";
    loadFelledBosses(c, p);
    loadFlags(c, p);
    loadInsights(c, p);
    loadDoorStates(c, p);
    loadInventory(c, p);
    loadNpcState(c, p);
    // Sangue. Absent on legacy saves (pre-currency-system) and on
    // fresh characters; both cases default to 0. value<uint32_t>
    // tolerates the field being absent or numeric of a smaller type.
    p.sangue_lifetime = c.value("sangue_lifetime", std::uint32_t{0});
    p.sangue_vessel = c.value("sangue_vessel", std::uint32_t{0});
    p.sangue_riversato = c.value("sangue_riversato", std::uint32_t{0});
    if (p.sangue_lifetime > SANGUE_LIFETIME_CAP)
        p.sangue_lifetime = SANGUE_LIFETIME_CAP;
    if (p.sangue_vessel > SANGUE_LIFETIME_CAP)
        p.sangue_vessel = SANGUE_LIFETIME_CAP;
    if (p.sangue_riversato > SANGUE_LIFETIME_CAP)
        p.sangue_riversato = SANGUE_LIFETIME_CAP;
    p.player_class = parsePlayerClass(c.value("player_class", std::string{}));
    return p;
}

void loadSettings(const json& j, SaveData& data)
{
    if (!j.contains("settings") || !j["settings"].is_object())
        return;
    const auto& s = j["settings"];
    data.settings.bgm_volume = s.value("bgm_volume", data.settings.bgm_volume);
    data.settings.sfx_volume = s.value("sfx_volume", data.settings.sfx_volume);
    data.settings.fov_degrees_third_person =
        s.value("fov_degrees_third_person", data.settings.fov_degrees_third_person);
    data.settings.fov_degrees_first_person =
        s.value("fov_degrees_first_person", data.settings.fov_degrees_first_person);
}
} // namespace

SaveData load(const std::string& path)
{
    const std::string resolved = path.empty() ? defaultSavePath() : path;
    SaveData data;
    std::ifstream file(resolved);
    if (!file.is_open())
        return data;
    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[SaveManager] Error parsing %s: %s -- using defaults\n",
                     resolved.c_str(), e.what());
        return data;
    }
    data.schema_version = j.value("schema_version", 1);
    migrate(data);
    if (j.contains("characters") && j["characters"].is_array())
    {
        for (const auto& c : j["characters"])
        {
            PlayerProfile p = loadCharacter(c);
            if (!p.name.empty())
                data.characters.push_back(std::move(p));
        }
    }
    loadSettings(j, data);
    std::fprintf(stderr, "[SaveManager] Loaded %zu characters from %s\n", data.characters.size(),
                 resolved.c_str());
    return data;
}

namespace
{
nlohmann::json saveDoorStates(const PlayerProfile& c)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& pair : c.door_states)
        arr.push_back({pair.first, pair.second});
    return arr;
}

nlohmann::json saveInventoryEntry(const selva::items::Entry& e)
{
    if (const auto* p = std::get_if<selva::items::PossessionEntry>(&e))
        return nlohmann::json{{"kind", "possession"}, {"id", p->item_id}};
    if (const auto* s = std::get_if<selva::items::StackEntry>(&e))
        return nlohmann::json{{"kind", "stack"}, {"id", s->item_id}, {"count", s->count}};
    if (const auto* x = std::get_if<selva::items::InstancedEntry>(&e))
        return nlohmann::json{
            {"kind", "instanced"}, {"id", x->item_id}, {"upgrade_level", x->upgrade_level}};
    return nlohmann::json::object();
}

nlohmann::json saveInventory(const PlayerProfile& c)
{
    nlohmann::json inv = nlohmann::json::object();
    for (const auto& [cat_id, entries] : c.inventory.by_category)
    {
        if (entries.empty())
            continue;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entries)
        {
            nlohmann::json je = saveInventoryEntry(e);
            if (!je.empty())
                arr.push_back(std::move(je));
        }
        if (!arr.empty())
            inv[cat_id] = std::move(arr);
    }
    return inv;
}

nlohmann::json saveNpcState(const PlayerProfile& c)
{
    nlohmann::json npcs = nlohmann::json::object();
    for (const auto& [npc_id, state] : c.npc_state)
    {
        nlohmann::json rec = nlohmann::json::object();
        rec["times_talked"] = state.times_talked;
        if (!state.topics_seen.empty())
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& t : state.topics_seen)
                arr.push_back(t);
            rec["topics_seen"] = std::move(arr);
        }
        npcs[npc_id] = std::move(rec);
    }
    return npcs;
}

nlohmann::json saveCharacter(const PlayerProfile& c)
{
    nlohmann::json char_json = {
        {"name", c.name},
        {"pos_x", c.pos_x},
        {"pos_y", c.pos_y},
        {"pos_z", c.pos_z},
        {"yaw", c.yaw},
        {"has_saved_pose", c.has_saved_pose},
        {"current_region_id", c.current_region_id.empty() ? "surface" : c.current_region_id},
    };
    // Skip emitting empty per-character fields so save files stay compact
    // (back-compat already tolerates absent keys on the load side).
    if (!c.felled_bosses.empty())
        char_json["felled_bosses"] = c.felled_bosses;
    if (!c.flags.empty())
        char_json["flags"] = c.flags;
    if (!c.unlocked_insights.empty())
        char_json["unlocked_insights"] = c.unlocked_insights;
    if (!c.kill_counts.empty())
    {
        json kc = json::object();
        for (const auto& [archetype, count] : c.kill_counts)
            kc[archetype] = count;
        char_json["kill_counts"] = std::move(kc);
    }
    if (!c.door_states.empty())
        char_json["door_states"] = saveDoorStates(c);
    if (!c.inventory.by_category.empty())
    {
        auto inv = saveInventory(c);
        if (!inv.empty())
            char_json["inventory"] = std::move(inv);
    }
    if (!c.npc_state.empty())
        char_json["npc_state"] = saveNpcState(c);
    // Sangue persists only when non-zero -- new characters and full-reclamation
    // states stay compact. sangue_vessel persists across quit-to-menu (a
    // run pause, not a death); second-death zeroes it before save.
    if (c.sangue_lifetime != 0u)
        char_json["sangue_lifetime"] = c.sangue_lifetime;
    if (c.sangue_vessel != 0u)
        char_json["sangue_vessel"] = c.sangue_vessel;
    if (c.sangue_riversato != 0u)
        char_json["sangue_riversato"] = c.sangue_riversato;
    if (c.player_class != PlayerClass::None)
        char_json["player_class"] = playerClassName(c.player_class);
    return char_json;
}

bool ensureSaveDir(const std::string& resolved)
{
    try
    {
        const std::filesystem::path dir = std::filesystem::path(resolved).parent_path();
        if (!dir.empty())
            std::filesystem::create_directories(dir);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[SaveManager] Cannot create directory for %s: %s\n", resolved.c_str(),
                     e.what());
        return false;
    }
    return true;
}
} // namespace

bool save(const SaveData& data, const std::string& path)
{
    const std::string resolved = path.empty() ? defaultSavePath() : path;
    if (!ensureSaveDir(resolved))
        return false;
    json j;
    j["schema_version"] = data.schema_version;
    j["characters"] = json::array();
    for (const auto& c : data.characters)
        j["characters"].push_back(saveCharacter(c));
    j["settings"] = {
        {"bgm_volume", data.settings.bgm_volume},
        {"sfx_volume", data.settings.sfx_volume},
        {"fov_degrees_third_person", data.settings.fov_degrees_third_person},
        {"fov_degrees_first_person", data.settings.fov_degrees_first_person},
    };
    std::ofstream file(resolved);
    if (!file.is_open())
    {
        std::fprintf(stderr, "[SaveManager] Cannot write %s\n", resolved.c_str());
        return false;
    }
    file << j.dump(4);
    std::fprintf(stderr, "[SaveManager] Saved to %s\n", resolved.c_str());
    return true;
}

void addCharacter(SaveData& data, const std::string& name)
{
    PlayerProfile p;
    p.name = name;
    data.characters.push_back(std::move(p));
}

void deleteCharacter(SaveData& data, const std::string& name)
{
    auto& chars = data.characters;
    chars.erase(std::remove_if(chars.begin(), chars.end(),
                               [&](const PlayerProfile& p) { return p.name == name; }),
                chars.end());
}

void migrate(SaveData& data)
{
    // v1 is the initial schema; no migration needed yet. Bump
    // SaveData::CURRENT_VERSION and add version-specific migration logic here
    // as fields are added (e.g. v1 -> v2 when class/stats land).
    data.schema_version = SaveData::CURRENT_VERSION;
}

} // namespace selva::SaveManager
