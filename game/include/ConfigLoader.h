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

    // Parse config/audio/music.json and populate MusicConfig in ctx.
    static bool loadMusic(EntityManager& em, const std::string& filePath);

    // Parse config/waves.json and populate WaveConfig in ctx.
    static bool loadWaves(EntityManager& em, const std::string& filePath);

    // Scan a directory (recursively) for item definition JSONs and populate
    // ItemRegistry in ctx. Call once at startup before loading entities.
    static bool loadItemDefs(EntityManager& em, const std::string& dirPath);

    // Scan a directory (recursively) for recipe JSONs and populate
    // RecipeRegistry in ctx.
    static bool loadRecipes(EntityManager& em, const std::string& dirPath);

    // Parse config/balance/scoring.json and populate ScoringConfig in ctx.
    static bool loadScoring(EntityManager& em, const std::string& filePath);

    // Parse config/balance/weapon_tiers.json and populate WeaponTierRegistry.
    static bool loadWeaponTiers(EntityManager& em, const std::string& filePath);

    // Scan config/evolution/ for per-family tree JSONs and populate EvolutionRegistry.
    static bool loadEvolutionTrees(EntityManager& em, const std::string& dirPath);

    // Parse config/appearance/layers.json and populate AppearanceConfig in ctx.
    static bool loadAppearanceConfig(EntityManager& em, const std::string& filePath);
};
