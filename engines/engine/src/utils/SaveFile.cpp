#include "utils/SaveFile.h"

#include <nlohmann/json.hpp>

#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace engine::save
{

std::string dir(const std::string& org, const std::string& app)
{
    // Cached per (org, app): SDL_GetPrefPath touches the filesystem (it creates the
    // directory), so repeat calls in a frame shouldn't pay for it.
    static std::unordered_map<std::string, std::string> cache;
    const std::string key = org + '/' + app;
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;

    std::string resolved = "saves/"; // fallback: always writable somewhere
    if (char* pref = SDL_GetPrefPath(org.c_str(), app.c_str()); pref != nullptr)
    {
        resolved = pref;
        SDL_free(pref);
    }
    cache[key] = resolved;
    return resolved;
}

std::string path(const std::string& org, const std::string& app, const std::string& file_name)
{
    return dir(org, app) + file_name;
}

std::optional<nlohmann::json> readJson(const std::string& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
        return std::nullopt; // no save yet -- a fresh start, not an error

    // Exceptions off: a corrupt/partial save reads as "no save" rather than
    // taking down the boot.
    nlohmann::json doc = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded())
    {
        std::fprintf(stderr, "[save] %s is unreadable -- treating as no save\n", file_path.c_str());
        return std::nullopt;
    }
    return doc;
}

bool writeJson(const nlohmann::json& doc, const std::string& file_path)
{
    std::error_code ec;
    const std::filesystem::path fs_path(file_path);
    if (fs_path.has_parent_path())
    {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
        if (ec)
        {
            std::fprintf(stderr, "[save] cannot create %s: %s\n",
                         fs_path.parent_path().string().c_str(), ec.message().c_str());
            return false;
        }
    }

    std::ofstream file(file_path);
    if (!file.is_open())
    {
        std::fprintf(stderr, "[save] cannot write %s\n", file_path.c_str());
        return false;
    }
    file << doc.dump(4);
    return file.good();
}

} // namespace engine::save
