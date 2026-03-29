#pragma once

#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"

// Emplace all game config singletons into the registry context.
// Call once per EntityManager in test setup. Safe to call before any
// system that reads FormulaConfig, SoundConfig, WaveConfig, or WaveState.
inline void emplaceGameConfigs(EntityManager& em)
{
    em.registry().ctx().emplace<FormulaConfig>();
    em.registry().ctx().emplace<SoundConfig>();
    em.registry().ctx().emplace<WaveConfig>();
    em.registry().ctx().emplace<WaveState>();
    em.registry().ctx().emplace<ItemRegistry>();
    em.registry().ctx().emplace<RecipeRegistry>();
    em.registry().ctx().emplace<MusicConfig>();
    em.registry().ctx().emplace<UIState>();
    em.registry().ctx().emplace<GameState>();
    em.registry().ctx().emplace<RunStats>();
    em.registry().ctx().emplace<ScoringConfig>();
    em.registry().ctx().emplace<SaveData>();
    em.registry().ctx().emplace<WeaponTierRegistry>();
    em.registry().ctx().emplace<EvolutionRegistry>();
    em.registry().ctx().emplace<Compendium>();
    em.registry().ctx().emplace<AttackTokenPool>();
}
