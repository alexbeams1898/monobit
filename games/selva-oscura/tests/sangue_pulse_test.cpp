// SanguePulse: auto-magnetization mechanic. On enemy death the
// dropped sangue amount decomposes into Roman-numeral additive tier
// particles (RomanTiers.h). Each particle stages, then flies in
// screen-space to the HUD vessel counter, then grants its
// denomination on arrival. Per
// [[project_crucible_censer_leveling_system]] auto-magnetization +
// the locked numeral-rendering doctrine.

#include "gameplay/RomanTiers.h"
#include "gameplay/SanguePulse.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

struct GrantSink
{
    std::uint32_t total = 0u;
    int call_count = 0;
};

void sinkGrant(std::uint32_t amount, void* ctx)
{
    auto* sink = static_cast<GrantSink*>(ctx);
    sink->total += amount;
    sink->call_count += 1;
}

constexpr glm::vec3 kCorpsePos{0.0f, 0.0f, 0.0f};
constexpr glm::vec2 kSourceScreen{800.0f, 400.0f};
constexpr glm::vec2 kTargetScreen{60.0f, 60.0f};

} // namespace

TEST_CASE("spawn for amount=1 yields exactly one particle", "[sangue-pulse]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1u);
    REQUIRE(selva::gameplay::sanguePulses().size() == 1);
    REQUIRE(selva::gameplay::sanguePulses()[0].denomination == 1u);
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("spawn for amount=0 is a no-op", "[sangue-pulse]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 0u);
    REQUIRE(selva::gameplay::sanguePulses().empty());
}

TEST_CASE("spawn for amount=1234 yields 10 particles (Roman additive decomp)",
          "[sangue-pulse][tier-decomp]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1234u);
    // 1234 = 1*M + 2*C + 3*X + 4*I = 10 particles per Roman tier rules.
    REQUIRE(selva::gameplay::sanguePulses().size() == 10);
    std::uint32_t total = 0u;
    for (const auto& p : selva::gameplay::sanguePulses())
        total += p.denomination;
    REQUIRE(total == 1234u);
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("spawn for amount=1000000 yields one mega-tier particle",
          "[sangue-pulse][tier-decomp][vinculum]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1000000u);
    REQUIRE(selva::gameplay::sanguePulses().size() == 1);
    REQUIRE(selva::gameplay::sanguePulses()[0].denomination == 1000000u);
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("tickSanguePulsesWith delivers + retires after lifetime+delay", "[sangue-pulse]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1u);
    GrantSink sink;
    // Single particle has no stagger window so delay=0; lifetime=0.55.
    // Advancing 1s in one tick completes it.
    selva::gameplay::tickSanguePulsesWith(1.0f, kTargetScreen, &sinkGrant, &sink);
    REQUIRE(sink.total == 1u);
    REQUIRE(sink.call_count == 1);
    REQUIRE(selva::gameplay::sanguePulses().empty());
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("a multi-particle spawn delivers the total amount across all particles", "[sangue-pulse]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 27u);
    // 27 = 2*10 + 1*5 + 2*1 = 5 particles.
    REQUIRE(selva::gameplay::sanguePulses().size() == 5);
    GrantSink sink;
    // Advance far enough that all particles deliver regardless of
    // stagger delay (stagger window is small for 5 particles).
    selva::gameplay::tickSanguePulsesWith(5.0f, kTargetScreen, &sinkGrant, &sink);
    REQUIRE(sink.total == 27u);
    REQUIRE(sink.call_count == 5);
    REQUIRE(selva::gameplay::sanguePulses().empty());
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("tier_index is consistent with the denomination ladder", "[sangue-pulse][tier-decomp]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1234u);
    for (const auto& p : selva::gameplay::sanguePulses())
    {
        REQUIRE(p.tier_index >= 0);
        REQUIRE(p.tier_index < selva::gameplay::kRomanTierCount);
        REQUIRE(selva::gameplay::kRomanTiers[p.tier_index] == p.denomination);
    }
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("particles in same spawn have varied stagger delays (stream effect)",
          "[sangue-pulse][stagger]")
{
    selva::gameplay::clearSanguePulsesForTest();
    // Use 33 which decomposes to 3*10 + 1*1 + 1*1 + 1*1 = 6 particles
    // (multiple, all denom=1 / denom=10, so we see real stagger).
    // 10 by itself is a single tier so it produces a single particle.
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 33u);
    REQUIRE(selva::gameplay::sanguePulses().size() == 6);
    // At least two particles should have different delays (stream
    // staggers them so they don't arrive simultaneously).
    bool found_varied = false;
    const float first_delay = selva::gameplay::sanguePulses()[0].delay;
    for (std::size_t i = 1; i < selva::gameplay::sanguePulses().size(); ++i)
    {
        if (selva::gameplay::sanguePulses()[i].delay != first_delay)
        {
            found_varied = true;
            break;
        }
    }
    REQUIRE(found_varied);
    selva::gameplay::clearSanguePulsesForTest();
}

TEST_CASE("delivered count is returned for HUD coordination", "[sangue-pulse]")
{
    selva::gameplay::clearSanguePulsesForTest();
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1u);
    selva::gameplay::spawnSanguePulses(kCorpsePos, kSourceScreen, kTargetScreen, 1u);
    GrantSink sink;
    REQUIRE(selva::gameplay::tickSanguePulsesWith(0.05f, kTargetScreen, &sinkGrant, &sink) == 0);
    REQUIRE(selva::gameplay::tickSanguePulsesWith(5.0f, kTargetScreen, &sinkGrant, &sink) == 2);
    REQUIRE(sink.total == 2u);
    selva::gameplay::clearSanguePulsesForTest();
}
