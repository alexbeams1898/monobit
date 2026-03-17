#include "systems/SpawnerSystem.h"

#include "ConfigLoader.h"
#include "TileMap.h"
#include "ecs/Components.h"
#include "systems/LevelingSystem.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

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

    // Collect all walkable tiles within the spawn distance band and pick one at random.
    // Sampling from known walkable positions guarantees a valid spawn every time —
    // the old angle→snap approach frequently failed on sparse maps where most of the
    // spawn radius circle falls inside wall-only areas.
    // Cost: O(map tiles) = O(4800) once every kSpawnInterval seconds — negligible.
    if (!em.tile_map.valid())
        return;

    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    const float nearSq = kSpawnNear * kSpawnNear;
    const float farSq = kSpawnFar * kSpawnFar;

    std::vector<std::pair<int, int>> candidates;
    for (int r = 0; r < em.tile_map.height; ++r)
    {
        for (int c = 0; c < em.tile_map.width; ++c)
        {
            if (!em.tile_map.at(c, r).walkable)
                continue;
            const float cx = static_cast<float>(c) * ts + ts * 0.5f;
            const float cy = static_cast<float>(r) * ts + ts * 0.5f;
            const float dx = cx - playerX;
            const float dy = cy - playerY;
            const float dSq = dx * dx + dy * dy;
            if (dSq >= nearSq && dSq <= farSq)
                candidates.emplace_back(c, r);
        }
    }

    if (candidates.empty())
        return;

    const auto& chosen = candidates[static_cast<std::size_t>(std::rand()) % candidates.size()];
    const float spawnX = static_cast<float>(chosen.first) * ts + ts * 0.5f;
    const float spawnY = static_cast<float>(chosen.second) * ts + ts * 0.5f;

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

    TracyMessageL("EnemySpawned");
    std::cout << "[SpawnerSystem] Spawned enemy at (" << spawnX << ", " << spawnY << ")\n";
}
