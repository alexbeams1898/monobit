#include "ConfigLoader.h"
#include "Engine.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "systems/LevelingSystem.h"
#include "systems/TileMapRenderer.h"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Engine engine;

    if (!engine.init("Hell Escape", 1280, 720))
        return 1;

    auto& em = engine.entityManager();

    // Load balance formulas first — all systems read from em.formulas.
    ConfigLoader::loadFormulas(em, "config/balance/formulas.json");

    // Generate the tile map — populates em.tile_map / em.tile_config and
    // returns the world-space centre of the first placed room (player spawn).
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms");
    TileMapRenderer::upload(em.tile_map); // bake static VBO — one draw call per frame

    // Player — placed at the first room's centre.
    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = px;
        t.y = py;
    }

    // Spawn entities from tile map markers.
    // 'E' → enemy, 'R' → rest spot.
    // Room templates author these; the generator collects them into
    // em.tile_map.spawn_points during generation.
    for (const auto& sp : em.tile_map.spawn_points)
    {
        const char* path = nullptr;
        if (sp.type == 'E')
            path = "config/entities/enemy.json";
        else if (sp.type == 'R')
            path = "config/entities/rest_spot.json";

        if (!path)
            continue;

        auto entity = ConfigLoader::loadEntity(em, path);
        if (!em.registry().valid(entity))
            continue;

        auto& t = em.registry().get<Transform>(entity);
        t.x = sp.x;
        t.y = sp.y;
    }

    if (em.registry().valid(player))
    {
        em.registry().emplace<Input>(player);
        const auto& pt = em.registry().get<Transform>(player);
        em.registry().emplace<Camera>(player, Camera{pt.x, pt.y, true});
    }

    // Derive Health.max from END stats for all stat-based entities.
    LevelingSystem::applyInitialDerivations(em);

    engine.run();
    return 0;
}
