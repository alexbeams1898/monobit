#pragma once

#include "ecs/EntityManager.h"

struct FormulaConfig;
struct SoundConfig;

// Allocate one stat point to the given stat and recalculate derived attributes.
void allocateStat(entt::registry& reg, entt::entity entity, int& stat, const FormulaConfig& f,
                  const SoundConfig& snd);

// ---------------------------------------------------------------------------
// LevelingSystem — XP-to-level-up logic and initial stat derivations.
//
// applyInitialDerivations(): call once at startup after all entities are
//   loaded.  Sweeps every entity with Stats, computes Health.max from END
//   via the formula, and either emplaces or overwrites Health.  This means
//   entity JSONs don't need a "health" block — it is fully derived.
//
// update(): runs every frame after PickupSystem.  Detects XP overflow,
//   increments level, awards stat points, and logs to console.  Also
//   processes debug stat-allocation key presses from PlayerActions.alloc_str/Dex/End/Lck.
// ---------------------------------------------------------------------------

class LevelingSystem
{
  public:
    // One-time startup pass: derive Health from Stats for all entities.
    static void applyInitialDerivations(EntityManager& em);

    // Derive Health from Stats for a single entity (e.g. after a dynamic spawn).
    // No-op if the entity has no Stats component.
    static void deriveHealth(EntityManager& em, entt::entity entity);

    // Derive Health, Stamina, and Poise from Stats for a single entity.
    // Call this for dynamically spawned entities that may carry any of those components.
    static void deriveInitialStats(EntityManager& em, entt::entity entity);

    // Per-frame update: XP → level-up, stat point allocation.
    static void update(EntityManager& em);
};
