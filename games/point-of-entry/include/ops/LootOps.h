#pragma once

#include "ecs/ItemConfig.h"

#include <random>
#include <vector>

// Rolling what a death pays in THINGS. Pure over its inputs -- the rng comes in as a
// parameter, so tests can hand it a fixed seed and the game can hand it a real one.
namespace loot
{

// Roll a whole table. Inspection is the finder stat: it nudges both whether a drop happens
// and how well it comes out.
std::vector<ItemInstance> roll(const std::vector<DropEntry>& table, int inspection,
                               std::mt19937& rng);

// The quality ladder for one drop, from a 0-50 score plus Inspection's nudge -- thresholds
// live in config (stats.json "loot"). Exposed for tests.
Quality qualityFor(float score);

} // namespace loot
