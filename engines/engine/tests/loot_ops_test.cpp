// Tests for engine::ops::loot::rollWeightedPool. The roll helper
// powers any system that needs weighted-distribution drops (today:
// Wood gather flow; future: enemy material drops, container loot).
// Determinism via fixed-seed std::mt19937 -- tests should not depend
// on system randomness.

#include "ecs/Items.h"
#include "ops/LootOps.h"

#include <random>
#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

using engine::ecs::WeightedEntry;
using engine::ecs::WeightedPool;
using engine::ops::loot::rollWeightedPool;

TEST_CASE("rollWeightedPool returns empty for empty pool", "[loot][weighted]")
{
    WeightedPool pool;
    std::mt19937 rng(0xA11CE);
    REQUIRE(rollWeightedPool(pool, rng).empty());
}

TEST_CASE("rollWeightedPool returns empty when all weights non-positive", "[loot][weighted]")
{
    WeightedPool pool;
    pool.entries.push_back({"a.json", 0});
    pool.entries.push_back({"b.json", -5});
    std::mt19937 rng(0xBEEF);
    REQUIRE(rollWeightedPool(pool, rng).empty());
}

TEST_CASE("rollWeightedPool always picks the only entry with positive weight", "[loot][weighted]")
{
    WeightedPool pool;
    pool.entries.push_back({"a.json", 0});
    pool.entries.push_back({"only.json", 7});
    pool.entries.push_back({"c.json", 0});
    std::mt19937 rng(42);
    for (int i = 0; i < 50; ++i)
        REQUIRE(rollWeightedPool(pool, rng) == "only.json");
}

TEST_CASE("rollWeightedPool single-entry pool returns that entry deterministically",
          "[loot][weighted]")
{
    WeightedPool pool;
    pool.entries.push_back({"sole.json", 1});
    std::mt19937 rng(1);
    REQUIRE(rollWeightedPool(pool, rng) == "sole.json");
    REQUIRE(rollWeightedPool(pool, rng) == "sole.json");
}

TEST_CASE("rollWeightedPool distribution matches weights over many rolls",
          "[loot][weighted][distribution]")
{
    // Wood forage's actual weights: 40 / 35 / 25 summing to 100.
    // Over 10000 rolls each entry should land within +/-5% of its
    // share. Fixed seed makes this deterministic across runs.
    WeightedPool pool;
    pool.entries.push_back({"bark.json", 40});
    pool.entries.push_back({"clay.json", 35});
    pool.entries.push_back({"lichen.json", 25});

    std::unordered_map<std::string, int> counts;
    std::mt19937 rng(2026);
    constexpr int kRolls = 10000;
    for (int i = 0; i < kRolls; ++i)
        counts[rollWeightedPool(pool, rng)]++;

    REQUIRE(counts["bark.json"] >= 3500);
    REQUIRE(counts["bark.json"] <= 4500);
    REQUIRE(counts["clay.json"] >= 3000);
    REQUIRE(counts["clay.json"] <= 4000);
    REQUIRE(counts["lichen.json"] >= 2000);
    REQUIRE(counts["lichen.json"] <= 3000);
    // Combined: only the three pool items appear, nothing else.
    REQUIRE(counts.size() == 3);
}

TEST_CASE("rollWeightedPool author can change one weight without retuning others",
          "[loot][weighted][authoring]")
{
    // Doubling bark's weight should approximately double its share.
    // Verifies the normalize-by-sum contract documented in the header.
    WeightedPool pool;
    pool.entries.push_back({"bark.json", 80});
    pool.entries.push_back({"clay.json", 35});
    pool.entries.push_back({"lichen.json", 25});

    std::unordered_map<std::string, int> counts;
    std::mt19937 rng(7);
    constexpr int kRolls = 10000;
    for (int i = 0; i < kRolls; ++i)
        counts[rollWeightedPool(pool, rng)]++;

    // Sum=140; bark share ~57%, clay ~25%, lichen ~18%. Wide bands.
    REQUIRE(counts["bark.json"] >= 5200);
    REQUIRE(counts["bark.json"] <= 6200);
    REQUIRE(counts["lichen.json"] >= 1500);
    REQUIRE(counts["lichen.json"] <= 2100);
}

TEST_CASE("rollWeightedPool ignores zero-weight entries amid positives", "[loot][weighted]")
{
    // Mixed zero + positive: zero entries should never come up.
    WeightedPool pool;
    pool.entries.push_back({"zero_a.json", 0});
    pool.entries.push_back({"real.json", 10});
    pool.entries.push_back({"zero_b.json", 0});
    pool.entries.push_back({"also_real.json", 10});

    std::unordered_map<std::string, int> counts;
    std::mt19937 rng(99);
    for (int i = 0; i < 5000; ++i)
        counts[rollWeightedPool(pool, rng)]++;

    REQUIRE(counts.count("zero_a.json") == 0);
    REQUIRE(counts.count("zero_b.json") == 0);
    REQUIRE(counts["real.json"] + counts["also_real.json"] == 5000);
}
