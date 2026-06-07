// Roman-tier decomposition: one particle = one Roman-numeral
// character's worth of substance. Conservation guarantee: sum of all
// particle denominations == original amount. Tier index drives the
// per-particle visual; denomination drives the grant on arrival.
//
// Pure-compute. No engine globals.

#include "gameplay/RomanTiers.h"

#include <numeric>

#include <catch2/catch_test_macros.hpp>

namespace
{

std::uint32_t sumDenoms(const std::vector<selva::gameplay::SangueParticleSpec>& parts)
{
    std::uint32_t total = 0u;
    for (const auto& p : parts)
        total += p.denomination;
    return total;
}

} // namespace

TEST_CASE("decompose 0 yields no particles", "[roman-tiers]")
{
    const auto parts = selva::gameplay::decomposeAmountToParticles(0u);
    REQUIRE(parts.empty());
}

TEST_CASE("decompose 1 yields a single denom=1 particle", "[roman-tiers]")
{
    const auto parts = selva::gameplay::decomposeAmountToParticles(1u);
    REQUIRE(parts.size() == 1);
    REQUIRE(parts[0].denomination == 1u);
    REQUIRE(parts[0].tier_index == 0);
}

TEST_CASE("decompose 5 yields one denom=5 particle, not five denom=1", "[roman-tiers][additive]")
{
    // Greedy peels largest tier first. 5 is itself a tier.
    const auto parts = selva::gameplay::decomposeAmountToParticles(5u);
    REQUIRE(parts.size() == 1);
    REQUIRE(parts[0].denomination == 5u);
}

TEST_CASE("decompose 4 yields 4 denom=1 particles (additive, NOT IV)", "[roman-tiers][additive]")
{
    // Subtractive Roman rendering happens at the counter, not in the
    // substance stream. Additive only here.
    const auto parts = selva::gameplay::decomposeAmountToParticles(4u);
    REQUIRE(parts.size() == 4);
    for (const auto& p : parts)
    {
        REQUIRE(p.denomination == 1u);
        REQUIRE(p.tier_index == 0);
    }
}

TEST_CASE("decompose 9 yields one 5 + four 1s (not IX)", "[roman-tiers][additive]")
{
    const auto parts = selva::gameplay::decomposeAmountToParticles(9u);
    REQUIRE(parts.size() == 5);
    REQUIRE(parts[0].denomination == 5u);
    for (std::size_t i = 1; i < parts.size(); ++i)
        REQUIRE(parts[i].denomination == 1u);
}

TEST_CASE("decompose 1234 yields 1*M + 2*C + 3*X + 4*I = 10 particles", "[roman-tiers]")
{
    const auto parts = selva::gameplay::decomposeAmountToParticles(1234u);
    REQUIRE(parts.size() == 10);
    REQUIRE(sumDenoms(parts) == 1234u);
    // Order: largest first.
    REQUIRE(parts[0].denomination == 1000u);
    REQUIRE(parts[1].denomination == 100u);
    REQUIRE(parts[2].denomination == 100u);
    REQUIRE(parts[3].denomination == 10u);
    REQUIRE(parts[4].denomination == 10u);
    REQUIRE(parts[5].denomination == 10u);
    REQUIRE(parts[6].denomination == 1u);
    REQUIRE(parts[7].denomination == 1u);
    REQUIRE(parts[8].denomination == 1u);
    REQUIRE(parts[9].denomination == 1u);
}

TEST_CASE("decompose 1000000 yields one particle of the millions tier", "[roman-tiers][vinculum]")
{
    const auto parts = selva::gameplay::decomposeAmountToParticles(1000000u);
    REQUIRE(parts.size() == 1);
    REQUIRE(parts[0].denomination == 1000000u);
    REQUIRE(sumDenoms(parts) == 1000000u);
}

TEST_CASE("conservation: sum of denominations always equals input", "[roman-tiers][conservation]")
{
    // Sample arbitrary amounts across the range and assert the
    // additive sum equals the input. The greedy algorithm guarantees
    // this; the test pins it.
    for (std::uint32_t amount :
         {1u, 7u, 13u, 42u, 99u, 500u, 1234u, 9999u, 10000u, 57892u, 999999u, 1000000u, 12345678u,
          99999999u, 100000000u, 500000000u, 999999999u})
    {
        const auto parts = selva::gameplay::decomposeAmountToParticles(amount);
        REQUIRE(sumDenoms(parts) == amount);
    }
}

TEST_CASE("decompose 3999 (max single-numeral additive form)", "[roman-tiers][edge]")
{
    // 3999 = MMMCMXCIX in subtractive form; in our additive form it's
    // 3*1000 + 9*100 + 9*10 + 9*1 -- but our decomp uses the larger
    // tiers first so 3999 = 3*1000 + 1*500 + 4*100 + 1*50 + 4*10 +
    // 1*5 + 4*1 = 18 particles.
    const auto parts = selva::gameplay::decomposeAmountToParticles(3999u);
    REQUIRE(sumDenoms(parts) == 3999u);
    // Counts per tier (largest down).
    int count_1000 = 0, count_500 = 0, count_100 = 0, count_50 = 0, count_10 = 0, count_5 = 0,
        count_1 = 0;
    for (const auto& p : parts)
    {
        if (p.denomination == 1000u)
            ++count_1000;
        else if (p.denomination == 500u)
            ++count_500;
        else if (p.denomination == 100u)
            ++count_100;
        else if (p.denomination == 50u)
            ++count_50;
        else if (p.denomination == 10u)
            ++count_10;
        else if (p.denomination == 5u)
            ++count_5;
        else if (p.denomination == 1u)
            ++count_1;
    }
    REQUIRE(count_1000 == 3);
    REQUIRE(count_500 == 1);
    REQUIRE(count_100 == 4);
    REQUIRE(count_50 == 1);
    REQUIRE(count_10 == 4);
    REQUIRE(count_5 == 1);
    REQUIRE(count_1 == 4);
}

TEST_CASE("tier index matches denomination position in kRomanTiers", "[roman-tiers]")
{
    // The renderer uses tier_index to pick color/size. Verify the
    // contract that tier_index correctly indexes into kRomanTiers.
    const auto parts = selva::gameplay::decomposeAmountToParticles(1234u);
    for (const auto& p : parts)
    {
        REQUIRE(p.tier_index >= 0);
        REQUIRE(p.tier_index < selva::gameplay::kRomanTierCount);
        REQUIRE(selva::gameplay::kRomanTiers[p.tier_index] == p.denomination);
    }
}
