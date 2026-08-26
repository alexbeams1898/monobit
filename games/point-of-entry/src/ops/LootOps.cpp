#include "ops/LootOps.h"

#include "ecs/BalanceConfig.h"

namespace loot
{

Quality qualityFor(float score)
{
    const auto& t = stats::formulas().loot.quality_thresholds;
    if (score < t[0])
        return Quality::Crude;
    if (score < t[1])
        return Quality::Standard;
    if (score < t[2])
        return Quality::Fine;
    return Quality::Superior;
}

std::vector<ItemInstance> roll(const std::vector<DropEntry>& table, int inspection,
                               std::mt19937& rng)
{
    const auto& f = stats::formulas().loot;
    std::vector<ItemInstance> out;
    std::uniform_real_distribution<float> chanceRoll(0.0f, 1.0f);
    std::uniform_int_distribution<int> scoreRoll(0, 50);

    for (const auto& entry : table)
    {
        // Inspection nudges the CHANCE multiplicatively -- the thorough man notices the drop
        // the hasty one tramples -- and the QUALITY additively, one score bump per point.
        const float chance =
            entry.chance * (1.0f + f.inspection_chance_scale * static_cast<float>(inspection - 1));
        if (chanceRoll(rng) > chance)
            continue;
        ItemInstance inst;
        inst.item = entry.item;
        std::uniform_int_distribution<int> qty(entry.min, entry.max);
        inst.count = qty(rng);
        const float score = static_cast<float>(scoreRoll(rng)) +
                            f.inspection_quality_scale * static_cast<float>(inspection - 1);
        inst.quality = qualityFor(score);
        out.push_back(std::move(inst));
    }
    return out;
}

} // namespace loot
