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
            PlayerProfile p;
            p.name = c.value("name", std::string{});
            p.pos_x = c.value("pos_x", 0.0f);
            p.pos_y = c.value("pos_y", 0.0f);
            p.pos_z = c.value("pos_z", 0.0f);
            p.yaw = c.value("yaw", 0.0f);
            p.has_saved_pose = c.value("has_saved_pose", false);
            // Scenes: missing field = "surface" for back-compat with
            // saves written before the Scenes system existed. Older
            // saves may reference "chapel_interior" or "acheron" —
            // those scenes were collapsed into "surface" so remap.
            p.current_region_id = c.value("current_region_id", std::string{"surface"});
            if (p.current_region_id == "chapel_interior" || p.current_region_id == "acheron")
                p.current_region_id = "surface";
            // Felled bosses: missing field = no bosses felled yet
            // (back-compat for saves written before the boss backend).
            if (c.contains("felled_bosses") && c["felled_bosses"].is_array())
            {
                for (const auto& b : c["felled_bosses"])
                {
                    if (b.is_string())
                        p.felled_bosses.push_back(b.get<std::string>());
                }
            }
            // Generic quest-state flags. Missing field = no flags set
            // (back-compat for saves written before the flag system
            // shipped). Same shape as felled_bosses.
            if (c.contains("flags") && c["flags"].is_array())
            {
                for (const auto& f : c["flags"])
                {
                    if (f.is_string())
                        p.flags.push_back(f.get<std::string>());
                }
            }
            // Door states. (door_id, state_name) pairs serialized as
            // an array of 2-element arrays (compact JSON, ordered).
            // Missing field = all doors at their JSON initial_state.
            if (c.contains("door_states") && c["door_states"].is_array())
            {
                for (const auto& entry : c["door_states"])
                {
                    if (entry.is_array() && entry.size() == 2 && entry[0].is_string() &&
                        entry[1].is_string())
                    {
                        p.door_states.emplace_back(entry[0].get<std::string>(),
                                                   entry[1].get<std::string>());
                    }
                }
            }
            // Inventory: by_category map of category_id -> entries.
            // Each entry is one of three kinds tagged by "kind".
            // Missing field = empty inventory (back-compat for v1 saves).
            if (c.contains("inventory") && c["inventory"].is_object())
            {
                for (const auto& [cat_id, entries] : c["inventory"].items())
                {
                    if (!entries.is_array())
                        continue;
                    auto& vec = p.inventory.by_category[cat_id];
                    for (const auto& e : entries)
                    {
                        if (!e.is_object() || !e.contains("kind") || !e.contains("id"))
                            continue;
                        const std::string kind = e.value("kind", std::string{});
                        const std::string id = e.value("id", std::string{});
                        if (kind == "possession")
                            vec.emplace_back(selva::items::PossessionEntry{id});
                        else if (kind == "stack")
                            vec.emplace_back(
                                selva::items::StackEntry{id, e.value("count", 0)});
                        else if (kind == "instanced")
                            vec.emplace_back(selva::items::InstancedEntry{
                                id, e.value("upgrade_level", 0)});
                    }
                }
            }
            if (!p.name.empty())
                data.characters.push_back(std::move(p));
        }
    }

    if (j.contains("settings") && j["settings"].is_object())
    {
        const auto& s = j["settings"];
        data.settings.bgm_volume = s.value("bgm_volume", data.settings.bgm_volume);
        data.settings.sfx_volume = s.value("sfx_volume", data.settings.sfx_volume);
        data.settings.fov_degrees_third_person =
            s.value("fov_degrees_third_person", data.settings.fov_degrees_third_person);
        data.settings.fov_degrees_first_person =
            s.value("fov_degrees_first_person", data.settings.fov_degrees_first_person);
    }

    std::fprintf(stderr, "[SaveManager] Loaded %zu characters from %s\n", data.characters.size(),
                 resolved.c_str());
    return data;
}

bool save(const SaveData& data, const std::string& path)
{
    const std::string resolved = path.empty() ? defaultSavePath() : path;
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

    json j;
    j["schema_version"] = data.schema_version;

    j["characters"] = json::array();
    for (const auto& c : data.characters)
    {
        json char_json = {
            {"name", c.name},
            {"pos_x", c.pos_x},
            {"pos_y", c.pos_y},
            {"pos_z", c.pos_z},
            {"yaw", c.yaw},
            {"has_saved_pose", c.has_saved_pose},
            {"current_region_id", c.current_region_id.empty() ? "surface" : c.current_region_id},
        };
        // Only emit felled_bosses if non-empty -- keeps save files
        // compact for characters who haven't killed any bosses yet.
        if (!c.felled_bosses.empty())
            char_json["felled_bosses"] = c.felled_bosses;
        // Only emit flags if non-empty (back-compat-friendly: a save
        // written before the flag system added the field has no
        // "flags" key, which loads as an empty vector).
        if (!c.flags.empty())
            char_json["flags"] = c.flags;
        // Door states. Compact 2-element array per entry.
        if (!c.door_states.empty())
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& pair : c.door_states)
                arr.push_back({pair.first, pair.second});
            char_json["door_states"] = std::move(arr);
        }
        // Inventory: only emit if any category has entries.
        if (!c.inventory.by_category.empty())
        {
            nlohmann::json inv = nlohmann::json::object();
            for (const auto& [cat_id, entries] : c.inventory.by_category)
            {
                if (entries.empty())
                    continue;
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& e : entries)
                {
                    if (const auto* p = std::get_if<selva::items::PossessionEntry>(&e))
                        arr.push_back({{"kind", "possession"}, {"id", p->item_id}});
                    else if (const auto* s = std::get_if<selva::items::StackEntry>(&e))
                        arr.push_back(
                            {{"kind", "stack"}, {"id", s->item_id}, {"count", s->count}});
                    else if (const auto* x = std::get_if<selva::items::InstancedEntry>(&e))
                        arr.push_back({{"kind", "instanced"},
                                       {"id", x->item_id},
                                       {"upgrade_level", x->upgrade_level}});
                }
                if (!arr.empty())
                    inv[cat_id] = std::move(arr);
            }
            if (!inv.empty())
                char_json["inventory"] = std::move(inv);
        }
        j["characters"].push_back(std::move(char_json));
    }

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
