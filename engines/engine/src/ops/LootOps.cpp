#include "ops/LootOps.h"

namespace engine::ops::loot
{

std::string rollWeightedPool(const engine::ecs::WeightedPool& pool, std::mt19937& rng)
{
    if (pool.entries.empty())
        return std::string{};
    int sum = 0;
    for (const auto& e : pool.entries)
        if (e.weight > 0)
            sum += e.weight;
    if (sum <= 0)
        return std::string{};
    std::uniform_int_distribution<int> dist(1, sum);
    int target = dist(rng);
    for (const auto& e : pool.entries)
    {
        if (e.weight <= 0)
            continue;
        target -= e.weight;
        if (target <= 0)
            return e.config_path;
    }
    return pool.entries.back().config_path;
}

} // namespace engine::ops::loot
