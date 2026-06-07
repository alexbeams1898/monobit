#include "gameplay/RomanTiers.h"

namespace selva::gameplay
{

std::vector<SangueParticleSpec> decomposeAmountToParticles(std::uint32_t amount)
{
    std::vector<SangueParticleSpec> out;
    if (amount == 0u)
        return out;
    // Walk tiers from largest to smallest, peeling off the count.
    for (int t = kRomanTierCount - 1; t >= 0; --t)
    {
        const std::uint32_t denom = kRomanTiers[t];
        if (denom > amount)
            continue;
        const std::uint32_t count = amount / denom;
        amount -= count * denom;
        out.reserve(out.size() + count);
        for (std::uint32_t i = 0; i < count; ++i)
            out.push_back(SangueParticleSpec{denom, t});
        if (amount == 0u)
            break;
    }
    return out;
}

} // namespace selva::gameplay
