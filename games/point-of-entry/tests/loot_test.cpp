#include "ecs/BalanceConfig.h"
#include "ops/LootOps.h"

#include <catch2/catch_test_macros.hpp>

// The loot promises: quality follows the thresholds, Inspection helps and never hurts, and a
// seeded roll is a repeatable roll.

TEST_CASE("quality follows the config thresholds", "[loot]")
{
    const auto& t = stats::formulas().loot.quality_thresholds;
    CHECK(loot::qualityFor(t[0] - 1.0f) == Quality::Crude);
    CHECK(loot::qualityFor(t[0]) == Quality::Standard);
    CHECK(loot::qualityFor(t[1]) == Quality::Fine);
    CHECK(loot::qualityFor(t[2]) == Quality::Superior);
}

TEST_CASE("inspection helps and never hurts", "[loot]")
{
    const std::vector<DropEntry> table{{"config/items/materials/chitin_flake.json", 1, 1, 0.4f}};
    int lowTotal = 0;
    int highTotal = 0;
    for (unsigned seed = 1; seed <= 50; ++seed)
    {
        std::mt19937 a(seed);
        std::mt19937 b(seed);
        lowTotal += static_cast<int>(loot::roll(table, 1, a).size());
        highTotal += static_cast<int>(loot::roll(table, 10, b).size());
    }
    CHECK(highTotal >= lowTotal);
}

TEST_CASE("a seeded roll is a repeatable roll", "[loot]")
{
    const std::vector<DropEntry> table{{"config/items/materials/chitin_flake.json", 1, 3, 1.0f}};
    std::mt19937 a(7);
    std::mt19937 b(7);
    const auto first = loot::roll(table, 3, a);
    const auto second = loot::roll(table, 3, b);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        CHECK(first[i].count == second[i].count);
        CHECK(first[i].quality == second[i].quality);
    }
}
