#include "systems/SpawnerSystem.h"

#include "ConfigLoader.h"
#include "ecs/Components.h"

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
