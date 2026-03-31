#pragma once

#include "ecs/EntityManager.h"

struct FormulaConfig;

// ---------------------------------------------------------------------------
// WeaponXPSystem -- grants weapon XP on kills and hits, processes level-ups.
//
// Weapon XP sources:
//   Kill: enemy power * kill_multiplier (called from DeathSystem)
//   Hit:  enemy power * hit_multiplier  (called from DamageSystem)
//
// Level-up applies per-level stat growth (base_damage + scaling) with
// quality-driven decay. XP curve steepens with level; quality softens it.
// ---------------------------------------------------------------------------

class WeaponXPSystem
{
  public:
    // Called each fixed tick to process pending level-ups.
    static void update(EntityManager& em);

    // Compute enemy power rating for weapon XP scaling.
    static float computeEnemyPower(int level, int max_hp, float base_damage, int total_stats,
                                   const FormulaConfig& f);

    // Grant weapon XP to the player's equipped weapon.
    // source_multiplier: kill_multiplier, hit_multiplier, or crit_multiplier.
    static void grantXP(EntityManager& em, float enemy_power, float source_multiplier);
};
