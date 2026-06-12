#pragma once

#include <cstdint>
#include <vector>

// Decomposition of a sangue amount into Roman-numeral additive tiers.
// One particle = one Roman-numeral character's worth of substance.
// Tiers are 1, 5, 10, 50, 100, 500, 1000, 5000, 10000, 50000, 100000,
// 500000, 1000000 -- the full ladder including vinculum'd thousands.
//
// Additive only: a +4 kill becomes 4 particles of denom=1, NOT 1 of 5
// minus 1 of 1. Subtractive Roman rendering happens at the counter,
// not in the substance stream. Particles are positive substance.
//
// The decomposition is greedy: peel off the largest tier first, then
// the next, etc. Total particles is bounded by the amount's
// Roman-numeral length in the additive form (e.g. amount=3999 ->
// 3*1000 + 9*100 + 9*10 + 9*1 = 30 particles max for a 4-digit amount).
// Cap not enforced here -- amounts >= the cosmological lifetime ceiling
// (999,999,999) decompose into <= 30 particles, which the renderer can
// stagger across the full effect duration.
//
// Per [[project_crucible_censer_leveling_system]] auto-magnetization +
// the locked numeral-rendering doctrine.

namespace selva::gameplay
{

// Tier denominations on the Roman additive ladder. Index 0 = smallest.
// Used both for decomposition and as the tier index passed to the
// renderer (tier index drives color/size).
inline constexpr std::uint32_t kRomanTiers[] = {
    1u,     5u,      10u,     50u,      100u,     500u,      1000u,     5000u,      10000u,
    50000u, 100000u, 500000u, 1000000u, 5000000u, 10000000u, 50000000u, 100000000u, 500000000u,
};

inline constexpr int kRomanTierCount = sizeof(kRomanTiers) / sizeof(kRomanTiers[0]);

// One particle worth of substance: its denomination + its tier index
// (an index into kRomanTiers). The tier index drives the per-particle
// visual (color/size); denomination drives the grant on arrival.
struct SangueParticleSpec
{
    std::uint32_t denomination = 0u;
    int tier_index = 0;
};

// Decompose `amount` into a list of particles using the Roman additive
// ladder. Returns an empty list for amount=0. Conservation guarantee:
// sum of all denominations == amount. Order: largest denominations
// first.
std::vector<SangueParticleSpec> decomposeAmountToParticles(std::uint32_t amount);

} // namespace selva::gameplay
