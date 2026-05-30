#include "world/RegionBootstrap.h"

#include "gameplay/Enemies.h"
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

engine::world::RegionId loadAllRegionsRegister()
{
    sDefaultSpawn = engine::world::kInvalidRegion;

    nlohmann::json registry;
    if (!readJson("assets/regions/regions.json", registry))
        return engine::world::kInvalidRegion;

    // Phase 1 of region loading. Doctrine: ALL regions preload at
    // boot, never mid-game. Soulslike gameplay can't tolerate mid-
    // session loading hitches; everything the player can transition
    // into during a session is resident from before the main menu
    // appears. Boot is allowed to take longer; gameplay frames are
    // sacrosanct.
    //
    // This phase: read each region.json, construct a JsonRegion (which
    // parses enemy_spawns + terrain_modifiers in its ctor), register
    // it into the engine RegionManager, and call registerModifiers()
    // so the terrain mesh builder sees authored modifiers when it
    // runs in initTerrain(). Phase 2 (loadAllRegionsPreload) runs
    // AFTER initTerrain to build per-mesh Jolt shapes for the terrain
    // and the region's static_meshes.
    //
    // Required-key contract: regions.json MUST contain "regions" (array
    // of region-id strings) and "default_spawn_region" (string). Missing
    // keys are a fatal boot-time error -- the loader refuses to silently
    // default, because a silently-defaulted empty regions array means
    // the player spawns into a world with no physics bodies and falls
    // through nothing. The fail-loudly mechanism is .at() which throws
    // on missing key; caught at this function's boundary and surfaced
    // as a refusal to boot.
    try
    {
        const auto& region_ids = registry.at("regions");
        if (!region_ids.is_array())
        {
            std::fprintf(stderr,
                         "[region-bootstrap] FATAL: regions.json 'regions' must be an array\n");
            return engine::world::kInvalidRegion;
        }
        for (const auto& sid_val : region_ids)
        {
            const std::string sid = sid_val.get<std::string>();
            const std::string folder = "assets/regions/" + sid;
            const std::string region_path = folder + "/region.json";
            nlohmann::json region_json;
            if (!readJson(region_path.c_str(), region_json))
            {
                std::fprintf(stderr, "[region-bootstrap] FATAL: region '%s' load failed\n",
                             sid.c_str());
                return engine::world::kInvalidRegion;
            }
            auto js = std::make_unique<JsonRegion>(region_json, folder);
            js->registerModifiers();
            const engine::world::RegionId reg_id = engine::world::registerRegion(std::move(js));
            std::fprintf(stderr, "[region-bootstrap] registered region '%s' as RegionId=%u\n",
                         sid.c_str(), reg_id.id);
        }

        const std::string default_id = registry.at("default_spawn_region").get<std::string>();
        sDefaultSpawn = engine::world::findRegionId(default_id.c_str());
        if (sDefaultSpawn == engine::world::kInvalidRegion)
        {
            std::fprintf(stderr,
                         "[region-bootstrap] FATAL: default_spawn_region '%s' is not a registered "
                         "region (check regions.json against the registered list above)\n",
                         default_id.c_str());
            return engine::world::kInvalidRegion;
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        std::fprintf(stderr,
                     "[region-bootstrap] FATAL: regions.json schema mismatch: %s. "
                     "Required keys: 'regions' (array), 'default_spawn_region' (string). See "
                     "assets/regions/SCHEMA.md.\n",
                     e.what());
        return engine::world::kInvalidRegion;
    }
    return sDefaultSpawn;
}

void loadAllRegionsPreload()
{
    // Phase 2 of region loading: build terrain Jolt shapes + .glb
    // mesh GPU buffers for every registered region. Runs AFTER
    // initTerrain() so the terrain mesh's CPU vertex arrays exist.
    for (int i = 0; i < engine::world::regionCount(); ++i)
    {
        auto* base = engine::world::regionPtr(engine::world::regionAt(i));
        auto* js = dynamic_cast<JsonRegion*>(base);
        if (js != nullptr)
            js->preloadAssets();
    }
}

engine::world::RegionId defaultSpawnRegion()
{
    return sDefaultSpawn;
}

void spawnActiveRegionEnemies()
{
    auto* base = engine::world::currentRegionPtr();
    if (base == nullptr)
    {
        std::fprintf(stderr,
                     "[region-bootstrap] spawnActiveRegionEnemies: no active region; skipping\n");
        return;
    }
    auto* js = dynamic_cast<JsonRegion*>(base);
    if (js == nullptr)
    {
        std::fprintf(stderr,
                     "[region-bootstrap] spawnActiveRegionEnemies: active region is not a "
                     "JsonRegion; skipping\n");
        return;
    }
    selva::gameplay::spawnRegionEnemies(js->regionId(), js->enemySpawnDecls());
}

} // namespace selva::world
