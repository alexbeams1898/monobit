#include "Surfaces.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace surfaces
{

bool Config::walkable(const std::string& name) const
{
    const std::string& key = name.empty() ? default_surface : name;
    if (const auto it = surfaces.find(key); it != surfaces.end())
        return it->second.walkable;
    if (const auto it = surfaces.find(default_surface); it != surfaces.end())
        return it->second.walkable;
    return true; // unknown surface + no default entry -> never trap the player
}

void load(Config& cfg, const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return; // defaults
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;

    cfg.default_surface = j.value("default", cfg.default_surface);
    if (const auto it = j.find("surfaces"); it != j.end() && it->is_object())
        for (const auto& [name, props] : it->items())
        {
            Surface s;
            s.walkable = props.value("walkable", true);
            cfg.surfaces[name] = s;
        }
}

} // namespace surfaces
