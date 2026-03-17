#include "systems/SpawnerSystem.h"

#include "ConfigLoader.h"
#include "ecs/Components.h"
#include "systems/LevelingSystem.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

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
    static constexpr float kSpawnInterval = 8.0f;  // seconds between spawns
    static constexpr float kSpawnDistance = 550.f; // pixels from player center
    static constexpr const char* kEnemyPath = "config/entities/correctional_officer.json";

    static float timer = kSpawnInterval;

    // Find the player position and check if they are moving.
    // Timer is frozen while the player is idle — no pent-up spawns when they start moving.
    float playerX = 0.f, playerY = 0.f;
    bool found = false;
    bool player_moving = false;
    for (auto e : em.registry().view<Input>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            const auto& inp = em.registry().get<Input>(e);
            playerX = t.x;
            playerY = t.y;
            player_moving = inp.move_x != 0.0f || inp.move_y != 0.0f;
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

    // Random angle → spawn position just outside camera view.
    const float angle = static_cast<float>(std::rand() % 360) * (3.14159f / 180.f);
    const float spawnX = playerX + std::cos(angle) * kSpawnDistance;
    const float spawnY = playerY + std::sin(angle) * kSpawnDistance;

    auto entity = ConfigLoader::loadEntity(em, kEnemyPath);
    if (!em.registry().valid(entity))
    {
        std::cerr << "[SpawnerSystem] Failed to spawn enemy from " << kEnemyPath << "\n";
        return;
    }

    auto& t = em.registry().get<Transform>(entity);
    t.x = spawnX;
    t.y = spawnY;

    LevelingSystem::deriveHealth(em, entity);

    std::cout << "[SpawnerSystem] Spawned enemy at (" << spawnX << ", " << spawnY << ")\n";
}
