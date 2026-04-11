#pragma once

// Umbrella header -- includes all game config sub-headers.
// Existing code can keep including this file unchanged.
// New code should prefer including only the sub-header it needs:
//   ecs/BalanceConfig.h -- FormulaConfig, SoundConfig, MusicConfig, WaveConfig, WaveState
//   ecs/ItemConfig.h    -- ItemDef, ItemRegistry, RecipeRegistry, WeaponTierRegistry,
//                          EvolutionRegistry, Compendium, Rarity, qualityName
//   ecs/AppState.h      -- UIState, GameState, RunStats, ScoringConfig, SaveData

#include "ecs/AppState.h"
#include "ecs/AppearanceConfig.h"
#include "ecs/BalanceConfig.h"
#include "ecs/ItemConfig.h"
