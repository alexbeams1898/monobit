#pragma once

#include "ecs/FormulaConfig.h"
#include "ecs/Items.h"

#include <string>

// JSON loaders for the engine-layer schemas (Items, FormulaConfig,
// RecipeRegistry, WeaponTierRegistry, EvolutionRegistry). Each loader
// takes the struct to populate + the file path, no EntityManager / ECS
// dependency. Games wire these into their own startup.
//
// Ported from games/prison-escape-game/src/ConfigLoader.cpp — the
// schemas were promoted to engine; the loaders follow.

namespace engine::ecs
{

// Load all balance formulas from JSON. Missing sub-objects fall through
// to the struct's compile-time defaults. Returns false if the file
// cannot be opened or fails to parse.
bool loadFormulaConfig(FormulaConfig& cfg, const std::string& file_path);

// Load every item template found in a directory (recursive). Each .json
// file becomes one ItemDef keyed by its repo-relative path. Returns the
// number of items loaded; sets registry.loaded = true even if empty.
int loadItemRegistry(ItemRegistry& registry, const std::string& dir_path);

// Load every recipe found in a directory (recursive).
int loadRecipeRegistry(RecipeRegistry& registry, const std::string& dir_path);

// Load per-weapon-tier defaults from a single JSON file.
bool loadWeaponTierRegistry(WeaponTierRegistry& registry, const std::string& file_path);

// Load every weapon evolution tree found in a directory (recursive).
int loadEvolutionRegistry(EvolutionRegistry& registry, const std::string& dir_path);

} // namespace engine::ecs
