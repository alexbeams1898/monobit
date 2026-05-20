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
            if (!p.name.empty())
                data.characters.push_back(std::move(p));
        }
    }

    if (j.contains("settings") && j["settings"].is_object())
    {
        const auto& s = j["settings"];
        data.settings.bgm_volume = s.value("bgm_volume", data.settings.bgm_volume);
        data.settings.sfx_volume = s.value("sfx_volume", data.settings.sfx_volume);
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
        j["characters"].push_back({{"name", c.name}});
    }

    j["settings"] = {
        {"bgm_volume", data.settings.bgm_volume},
        {"sfx_volume", data.settings.sfx_volume},
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
