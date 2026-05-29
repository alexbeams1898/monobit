#include "world/RegionBootstrap.h"

#include "world/JsonRegion.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <memory>

namespace selva::world
{

namespace
{
engine::world::RegionId sDefaultSpawn = engine::world::kInvalidRegion;

bool readJson(const char* path, nlohmann::json& out)
{
    std::ifstream f(path);
    if (!f)
    {
        std::fprintf(stderr, "[region-bootstrap] cannot open %s\n", path);
        return false;
    }
    try
    {
        f >> out;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[region-bootstrap] parse error in %s: %s\n", path, e.what());
        return false;
    }
    return true;
}

} // namespace

engine::world::RegionId loadAllRegions()
{
    sDefaultSpawn = engine::world::kInvalidRegion;

    nlohmann::json registry;
    if (!readJson("assets/regions/regions.json", registry))
        return engine::world::kInvalidRegion;

    // Doctrine: ALL scenes preload at boot, never mid-game. Soulslike
    // gameplay can't tolerate mid-session loading hitches; everything
    // the player can transition into during a session is resident from
    // before the main menu appears. Boot is allowed to take longer;
    // gameplay frames are sacrosanct.
    const auto& region_ids = registry.value("scenes", nlohmann::json::array());
    for (const auto& sid_val : region_ids)
    {
        const std::string sid = sid_val.get<std::string>();
        const std::string folder = "assets/regions/" + sid;
        const std::string region_path = folder + "/region.json";
        nlohmann::json region_json;
        if (!readJson(region_path.c_str(), region_json))
        {
            std::fprintf(stderr, "[region-bootstrap] skipping region '%s' (load failed)\n",
                         sid.c_str());
            continue;
        }
        auto js = std::make_unique<JsonRegion>(region_json, folder);
        js->preloadAssets();
        const engine::world::RegionId reg_id = engine::world::registerRegion(std::move(js));
        std::fprintf(stderr, "[region-bootstrap] registered region '%s' as RegionId=%u\n",
                     sid.c_str(), reg_id.id);
    }

    const std::string default_id = registry.value("default_spawn_scene", std::string{});
    sDefaultSpawn = engine::world::findRegionId(default_id.c_str());
    if (sDefaultSpawn == engine::world::kInvalidRegion)
    {
        std::fprintf(stderr, "[region-bootstrap] no valid default_spawn_scene '%s'\n",
                     default_id.c_str());
    }
    return sDefaultSpawn;
}

engine::world::RegionId defaultSpawnRegion()
{
    return sDefaultSpawn;
}

} // namespace selva::world
