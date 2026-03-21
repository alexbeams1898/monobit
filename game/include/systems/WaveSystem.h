#pragma once

#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"

class WaveSystem
{
  public:
    // Per-frame update: spawn enemies, detect wave clear, advance state machine.
    static void update(EntityManager& em, double dt);

    // Start the next wave. Only callable from Idle or SafeRoom phase.
    // Returns false if max_waves reached or phase doesn't allow starting.
    static bool startNextWave(EntityManager& em);

    // Build an ActiveWave for the given wave number from auto-gen rules.
    // Public so tests can verify generation formulas directly.
    static ActiveWave generateWave(const WaveGenRules& gen, int wave_number);
};
