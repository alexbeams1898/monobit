#pragma once

#include "ecs/Items.h"

#include <random>
#include <string>

// Engine-side loot operations. Pure functions over the loot data
// primitives in ecs/Items.h (DropEntry / Loot for independent-roll
// drop tables, WeightedPool for pick-one-weighted-distribution
// drops). Game-side code wraps these with its per-source cadence
// (gather spawner, enemy death system, etc).

namespace engine::ops::loot
{

// Roll one entry from a weighted pool. Returns the chosen entry's
// config_path (empty string if pool is empty or all weights are
// non-positive). RNG is passed in so tests can determinize without
// touching globals.
//
// Normalization: weights are interpreted relative to their sum. An
// entry with weight 40 in a pool summing to 100 is picked 40% of
// the time. Author-friendly: edit one weight without re-balancing
// others.
std::string rollWeightedPool(const engine::ecs::WeightedPool& pool, std::mt19937& rng);

} // namespace engine::ops::loot
