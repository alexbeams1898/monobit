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

// Load the player's unlocked insight node ids.
void loadInsightUnlockedNodes(const json& c, PlayerProfile& p)
{
    if (!c.contains("unlocked_insights") || !c["unlocked_insights"].is_array())
        return;
    for (const auto& n : c["unlocked_insights"])
        if (n.is_string())
            p.unlocked_insights.push_back(n.get<std::string>());
}

// Load a per-key uint32 map field (kill_counts, examine_counts).
void loadInsightCountMap(const json& c, const char* key,
                         std::unordered_map<std::string, std::uint32_t>& dst)
{
    if (!c.contains(key) || !c[key].is_object())
        return;
    for (auto it = c[key].begin(); it != c[key].end(); ++it)
        if (it.value().is_number_unsigned())
            dst[it.key()] = it.value().get<std::uint32_t>();
}

// Load certain_conclusions, which only older saves carry.
void loadCertainConclusions(const json& c, PlayerProfile& p)
{
    if (!c.contains("certain_conclusions") || !c["certain_conclusions"].is_array())
        return;
    for (const auto& n : c["certain_conclusions"])
        if (n.is_string())
            p.certain_conclusions.push_back(n.get<std::string>());
}

// Load an array-of-strings JSON field into a string vector.
void loadStringArrayField(const json& n, const char* key, std::vector<std::string>& dst)
{
    if (!n.contains(key) || !n[key].is_array())
        return;
    for (const auto& s : n[key])
        if (s.is_string())
            dst.push_back(s.get<std::string>());
}

// Load a single workbench node object into a WorkbenchNode struct.
// Returns false if the node is missing its required `id` field.
bool loadWorkbenchOne(const json& n, PlayerProfile::WorkbenchNode& w)
{
    if (!n.is_object() || !n.contains("id") || !n["id"].is_string())
        return false;
    w.id = n["id"].get<std::string>();
    if (n.contains("x") && n["x"].is_number())
        w.x = n["x"].get<float>();
    if (n.contains("y") && n["y"].is_number())
        w.y = n["y"].get<float>();
    loadStringArrayField(n, "linked_observations", w.linked_observations);
    if (n.contains("reading_id") && n["reading_id"].is_string())
        w.reading_id = n["reading_id"].get<std::string>();
    loadStringArrayField(n, "linked_confirmers", w.linked_confirmers);
    return true;
}

// Load one workbench-node array into the destination vector.
void loadWorkbenchVec(const json& arr, std::vector<PlayerProfile::WorkbenchNode>& out)
{
    for (const auto& n : arr)
    {
        PlayerProfile::WorkbenchNode w;
        if (loadWorkbenchOne(n, w))
            out.push_back(std::move(w));
    }
}

// Load workbench observations + inferences. Older saves name the same
// list `workbench_conclusions`; both are read.
void loadWorkbenchNodes(const json& c, PlayerProfile& p)
{
    if (c.contains("workbench_observations") && c["workbench_observations"].is_array())
        loadWorkbenchVec(c["workbench_observations"], p.workbench_observations);
    if (c.contains("workbench_inferences") && c["workbench_inferences"].is_array())
        loadWorkbenchVec(c["workbench_inferences"], p.workbench_inferences);
    else if (c.contains("workbench_conclusions") && c["workbench_conclusions"].is_array())
        loadWorkbenchVec(c["workbench_conclusions"], p.workbench_inferences);
}

void loadInsights(const json& c, PlayerProfile& p)
{
    loadInsightUnlockedNodes(c, p);
    loadInsightCountMap(c, "kill_counts", p.kill_counts);
    loadCertainConclusions(c, p);
    loadInsightCountMap(c, "examine_counts", p.examine_counts);
    loadWorkbenchNodes(c, p);
    p.perception_growth = c.value("perception_growth", std::uint32_t{0});
    p.cognition_growth = c.value("cognition_growth", std::uint32_t{0});
    p.intelligence_growth = c.value("intelligence_growth", std::uint32_t{0});
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

// Serialize a string-keyed uint32 map (kill_counts / examine_counts).
json saveCountMap(const std::unordered_map<std::string, std::uint32_t>& src)
{
    json out = json::object();
    for (const auto& [key, count] : src)
        out[key] = count;
    return out;
}

// Serialize one workbench-node vector to a JSON array.
json saveWorkbenchVec(const std::vector<PlayerProfile::WorkbenchNode>& v)
{
    json arr = json::array();
    for (const auto& w : v)
    {
        json o = json::object();
        o["id"] = w.id;
        o["x"] = w.x;
        o["y"] = w.y;
        if (!w.linked_observations.empty())
            o["linked_observations"] = w.linked_observations;
        if (!w.reading_id.empty())
            o["reading_id"] = w.reading_id;
        arr.push_back(std::move(o));
    }
    return arr;
}

// Write the optional insight-system fields (counts, conclusions,
// growth counters, workbench nodes). Skip-emit-when-empty keeps
// save files compact.
void saveInsightFields(nlohmann::json& dst, const PlayerProfile& c)
{
    if (!c.unlocked_insights.empty())
        dst["unlocked_insights"] = c.unlocked_insights;
    if (!c.kill_counts.empty())
        dst["kill_counts"] = saveCountMap(c.kill_counts);
    if (!c.certain_conclusions.empty())
        dst["certain_conclusions"] = c.certain_conclusions;
    if (!c.examine_counts.empty())
        dst["examine_counts"] = saveCountMap(c.examine_counts);
    if (c.perception_growth > 0)
        dst["perception_growth"] = c.perception_growth;
    if (c.cognition_growth > 0)
        dst["cognition_growth"] = c.cognition_growth;
    if (c.intelligence_growth > 0)
        dst["intelligence_growth"] = c.intelligence_growth;
    if (!c.workbench_observations.empty())
        dst["workbench_observations"] = saveWorkbenchVec(c.workbench_observations);
    if (!c.workbench_inferences.empty())
        dst["workbench_inferences"] = saveWorkbenchVec(c.workbench_inferences);
}

// Write the optional sangue economy fields. New characters and
// full-reclamation states stay compact.
void saveSangueFields(nlohmann::json& dst, const PlayerProfile& c)
{
    if (c.sangue_lifetime != 0u)
        dst["sangue_lifetime"] = c.sangue_lifetime;
    if (c.sangue_vessel != 0u)
        dst["sangue_vessel"] = c.sangue_vessel;
    if (c.sangue_riversato != 0u)
        dst["sangue_riversato"] = c.sangue_riversato;
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
    if (!c.felled_bosses.empty())
        char_json["felled_bosses"] = c.felled_bosses;
    if (!c.flags.empty())
        char_json["flags"] = c.flags;
    saveInsightFields(char_json, c);
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
    saveSangueFields(char_json, c);
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
