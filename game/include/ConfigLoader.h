#pragma once

#include "ecs/EntityManager.h"

#include <string>

// ---------------------------------------------------------------------------
// ConfigLoader -- reads a JSON entity definition and spawns it into the ECS.
// Game-side code: knows about game components and config schemas.
// ---------------------------------------------------------------------------

class ConfigLoader
{
  public:
    static entt::entity loadEntity(EntityManager& em, const std::string& filePath);

    // Parse config/balance/formulas.json and populate FormulaConfig in ctx.
    static bool loadFormulas(EntityManager& em, const std::string& filePath);

    // Parse config/audio/sounds.json and populate SoundConfig in ctx.
    static bool loadSounds(EntityManager& em, const std::string& filePath);

    // Parse config/waves.json and populate WaveConfig in ctx.
    static bool loadWaves(EntityManager& em, const std::string& filePath);
};
