#include "systems/SpawnerSystem.h"

#include "ConfigLoader.h"
#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ops/SpawnUtils.h"
#include "systems/LevelingSystem.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <tracy/Tracy.hpp>

using json = nlohmann::json;

int SpawnerSystem::load(EntityManager& em, const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        std::cerr << "SpawnerSystem: cannot open " << configPath << "\n";
        return 0;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        std::cerr << "SpawnerSystem: parse error in " << configPath << ": " << e.what() << "\n";
        return 0;
    }

    if (!j.contains("spawns") || !j["spawns"].is_array())
    {
        std::cerr << "SpawnerSystem: missing 'spawns' array in " << configPath << "\n";
        return 0;
    }

    int count = 0;
    for (const auto& spawn : j["spawns"])
    {
        const std::string entityPath = spawn.value("entity", "");
        if (entityPath.empty())
            continue;

        auto entity = ConfigLoader::loadEntity(em, entityPath);
        if (!em.registry().valid(entity))
            continue;

        auto& t = em.registry().get<Transform>(entity);
        t.x = spawn.value("x", t.x);
        t.y = spawn.value("y", t.y);

        ++count;
    }

    return count;
}

void SpawnerSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("SpawnerSystem");
    static constexpr float kSpawnInterval = 4.0f; // seconds between spawns
    static constexpr float kSpawnNear = 330.f;    // min spawn distance from player (px)
    static constexpr float kSpawnFar = 825.f;     // max spawn distance from player (px)
    static constexpr const char* kEnemyPath = "config/entities/skeleton.json";

    static float timer = kSpawnInterval;

    // Find the player position and check if they are moving.
    // Timer is frozen while the player is idle — no pent-up spawns when they start moving.
    float playerX = 0.f, playerY = 0.f;
    bool found = false;
    bool player_moving = false;
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            const auto& actions = em.registry().get<PlayerActions>(e);
            playerX = t.x;
            playerY = t.y;
            player_moving = actions.move_x != 0.0f || actions.move_y != 0.0f;
            found = true;
        }
        break;
    }
    if (!found || !player_moving)
        return;

    timer -= static_cast<float>(dt);
    if (timer > 0.f)
        return;
    timer = kSpawnInterval;

    float spawnX = 0.0f;
    float spawnY = 0.0f;
    if (!SpawnUtils::findSpawnPosition(em.tile_map, playerX, playerY, kSpawnNear, kSpawnFar, spawnX,
                                       spawnY))
        return;

    auto entity = ConfigLoader::loadEntity(em, kEnemyPath);
    if (!em.registry().valid(entity))
    {
        std::cerr << "[SpawnerSystem] Failed to spawn enemy from " << kEnemyPath << "\n";
        return;
    }

    auto& t = em.registry().get<Transform>(entity);
    t.x = spawnX;
    t.y = spawnY;

    LevelingSystem::deriveInitialStats(em, entity);

    TracyMessageL("EnemySpawned");
    std::cout << "[SpawnerSystem] Spawned enemy at (" << spawnX << ", " << spawnY << ")\n";
}
