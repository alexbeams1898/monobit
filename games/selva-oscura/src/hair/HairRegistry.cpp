#include "hair/HairRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace selva::hair
{

namespace
{
HairRegistry& mutableRegistry()
{
    static HairRegistry g;
    return g;
}
} // namespace

const HairRegistry& hairRegistry()
{
    return mutableRegistry();
}

void loadHairRegistry(const std::string& config_path)
{
    HairRegistry& reg = mutableRegistry();
    reg.styles.clear();
    reg.by_id.clear();

    if (!std::filesystem::exists(config_path))
    {
        std::fprintf(stderr, "[hair] registry not found: %s (empty registry)\n",
                     config_path.c_str());
        std::fflush(stderr);
        return;
    }
    std::ifstream f(config_path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[hair] failed to open: %s\n", config_path.c_str());
        std::fflush(stderr);
        return;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[hair] parse error in %s: %s\n", config_path.c_str(), e.what());
        std::fflush(stderr);
        return;
    }
    if (!j.contains("styles") || !j["styles"].is_array())
    {
        std::fprintf(stderr, "[hair] %s missing 'styles' array\n", config_path.c_str());
        std::fflush(stderr);
        return;
    }
    for (const auto& entry : j["styles"])
    {
        if (!entry.contains("id") || !entry["id"].is_string())
            continue;
        HairStyle s;
        s.id = entry["id"].get<std::string>();
        if (entry.contains("display_name") && entry["display_name"].is_string())
            s.display_name = entry["display_name"].get<std::string>();
        if (entry.contains("mesh_path_male") && entry["mesh_path_male"].is_string())
            s.mesh_path_male = entry["mesh_path_male"].get<std::string>();
        if (entry.contains("license") && entry["license"].is_string())
            s.license = entry["license"].get<std::string>();
        if (entry.contains("length") && entry["length"].is_string())
            s.length = entry["length"].get<std::string>();
        reg.by_id[s.id] = reg.styles.size();
        reg.styles.push_back(std::move(s));
    }
    std::fprintf(stderr, "[hair] loaded %zu style(s) from %s\n", reg.styles.size(),
                 config_path.c_str());
    std::fflush(stderr);
}

const HairStyle* findHairStyle(const std::string& id)
{
    if (id.empty())
        return nullptr;
    const HairRegistry& reg = hairRegistry();
    auto it = reg.by_id.find(id);
    if (it == reg.by_id.end())
        return nullptr;
    return &reg.styles[it->second];
}

} // namespace selva::hair
