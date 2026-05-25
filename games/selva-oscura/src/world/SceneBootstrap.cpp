#include "world/SceneBootstrap.h"

#include "world/JsonScene.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <memory>

namespace selva::world
{

namespace
{
engine::world::SceneId sDefaultSpawn = engine::world::kInvalidScene;

bool readJson(const char* path, nlohmann::json& out)
{
    std::ifstream f(path);
    if (!f)
    {
        std::fprintf(stderr, "[scene-bootstrap] cannot open %s\n", path);
        return false;
    }
    try
    {
        f >> out;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[scene-bootstrap] parse error in %s: %s\n", path, e.what());
        return false;
    }
    return true;
}

} // namespace

engine::world::SceneId loadAllScenes()
{
    sDefaultSpawn = engine::world::kInvalidScene;

    nlohmann::json registry;
    if (!readJson("assets/scenes/scenes.json", registry))
        return engine::world::kInvalidScene;

    const auto& scene_ids = registry.value("scenes", nlohmann::json::array());
    for (const auto& sid_val : scene_ids)
    {
        const std::string sid = sid_val.get<std::string>();
        const std::string folder = "assets/scenes/" + sid;
        const std::string scene_path = folder + "/scene.json";
        nlohmann::json scene_json;
        if (!readJson(scene_path.c_str(), scene_json))
        {
            std::fprintf(stderr, "[scene-bootstrap] skipping scene '%s' (load failed)\n",
                         sid.c_str());
            continue;
        }
        auto js = std::make_unique<JsonScene>(scene_json, folder);
        // Pre-load all assets at boot. Resident-all-scenes model:
        // file I/O + GL upload happens NOW, once. Transitions are
        // pure body-swap (sub-ms), no load stall.
        js->preloadAssets();
        const engine::world::SceneId reg_id = engine::world::registerScene(std::move(js));
        std::fprintf(stderr, "[scene-bootstrap] registered scene '%s' as SceneId=%u\n",
                     sid.c_str(), reg_id.id);
    }

    const std::string default_id = registry.value("default_spawn_scene", std::string{});
    sDefaultSpawn = engine::world::findSceneId(default_id.c_str());
    if (sDefaultSpawn == engine::world::kInvalidScene)
    {
        std::fprintf(stderr, "[scene-bootstrap] no valid default_spawn_scene '%s'\n",
                     default_id.c_str());
    }
    return sDefaultSpawn;
}

engine::world::SceneId defaultSpawnScene() { return sDefaultSpawn; }

} // namespace selva::world
