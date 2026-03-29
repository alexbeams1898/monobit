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
    // Level 0 = base grid size (wave 1).
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms", 0, 0);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Spawn rest spots at 'R' markers, player at 'P' marker (fallback to 'R').
    float spawnX = px;
    float spawnY = py;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'R')
        {
            auto entity = ConfigLoader::loadEntity(em, "config/entities/rest_spot.json");
            if (!em.registry().valid(entity))
                continue;
            auto& t = em.registry().get<Transform>(entity);
            t.x = sp.x;
            t.y = sp.y;
            if (spawnX == px && spawnY == py)
            {
                spawnX = sp.x;
                spawnY = sp.y;
            }
        }
        else if (sp.type == 'P')
        {
            spawnX = sp.x;
            spawnY = sp.y;
        }
    }

    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = spawnX;
        t.y = spawnY;
        em.registry().emplace<PlayerActions>(player);
        auto& wallet = em.registry().emplace<Wallet>(player);
        const auto& saveData = em.registry().ctx().get<SaveData>();
        const auto& charName = em.registry().ctx().get<GameState>().active_character;
        for (const auto& prof : saveData.characters)
        {
            if (prof.name == charName)
            {
                wallet.money = prof.money;
                break;
            }
        }
        em.registry().emplace<InteractTarget>(player);
        em.registry().emplace<Camera>(player, Camera{spawnX, spawnY, true});
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

    // Clear the tile map so it doesn't render behind the main menu.
    em.tile_map = TileMap{};
    TileMapRenderer::clear();

    // Reset wave state for the next run.
    reg.ctx().get<WaveState>() = WaveState{};
    reg.ctx().get<RunStats>() = RunStats{};
    reg.ctx().get<AttackTokenPool>().holders.clear();

    em.registry().ctx().get<GameState>().world_initialized = false;
}

} // namespace WorldInit
