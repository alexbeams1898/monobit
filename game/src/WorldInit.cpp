#include "WorldInit.h"

#include "ConfigLoader.h"
#include "Engine.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/LevelingSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/WaveSystem.h"

#include <vector>

namespace WorldInit
{

void createWorld(Engine& engine, EntityManager& em)
{
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms");
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    float restX = px;
    float restY = py;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type != 'R')
            continue;

        auto entity = ConfigLoader::loadEntity(em, "config/entities/rest_spot.json");
        if (!em.registry().valid(entity))
            continue;

        auto& t = em.registry().get<Transform>(entity);
        t.x = sp.x;
        t.y = sp.y;
        restX = sp.x;
        restY = sp.y;
    }

    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = restX;
        t.y = restY;
        em.registry().emplace<PlayerActions>(player);
        em.registry().emplace<Wallet>(player);
        em.registry().emplace<InteractTarget>(player);
        em.registry().emplace<Camera>(player, Camera{restX, restY, true});
    }

    LevelingSystem::applyInitialDerivations(em);

    // Reset run stats for the new run.
    em.registry().ctx().get<RunStats>() = RunStats{};

    WaveSystem::startNextWave(em);

    em.registry().ctx().get<GameState>().world_initialized = true;
}

void destroyWorld(EntityManager& em)
{
    auto& reg = em.registry();

    // Destroy all entities (players, enemies, body parts, pickups, rest spots, etc.).
    std::vector<entt::entity> all;
    for (auto e : reg.storage<entt::entity>())
        all.push_back(e);
    for (auto e : all)
        if (reg.valid(e))
            reg.destroy(e);

    // Reset wave state for the next run.
    reg.ctx().get<WaveState>() = WaveState{};
    reg.ctx().get<RunStats>() = RunStats{};

    em.registry().ctx().get<GameState>().world_initialized = false;
}

} // namespace WorldInit
