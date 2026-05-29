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
        j["characters"].push_back({
            {"name", c.name},
            {"pos_x", c.pos_x},
            {"pos_y", c.pos_y},
            {"pos_z", c.pos_z},
            {"yaw", c.yaw},
            {"has_saved_pose", c.has_saved_pose},
            {"current_region_id", c.current_region_id.empty() ? "surface" : c.current_region_id},
        });
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
