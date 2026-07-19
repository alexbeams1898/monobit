#include "Crafting.h"
#include "Inventory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using crafting::Ingredient;
using crafting::OutputKind;
using crafting::Recipe;

namespace
{
const observations::RollRng kZero = [](int) { return 0; };  // luck roll = 0 (min)
const observations::RollRng kMax = [](int n) { return n; }; // luck roll = n (max)

// A recipe: 2 thyme + 1 water -> tea; scales on "survival". No gate -- ingredients are the only
// requirement.
Recipe teaRecipe()
{
    Recipe r;
    r.id = "tea";
    r.inputs = {Ingredient{"thyme", 2}, Ingredient{"water", 1}};
    r.output_item = "tea";
    r.output_qty = 1;
    r.kind = OutputKind::Consumable;
    r.scaling.stat = "survival";
    r.scaling.base = 1.0f;
    r.xp_stats = {crafting::StatShare{"survival", 7.0f},
                  crafting::StatShare{"craftsmanship", 3.0f}};
    r.xp_base = 10;
    r.reveals_flag = "brewed_tea_once";
    return r;
}
} // namespace

TEST_CASE("match finds an exact type-set recipe; else the nearest by overlap", "[crafting]")
{
    crafting::Registry reg;
    reg.recipes["tea"] = teaRecipe();

    // Exact type set (thyme + water) -> the recipe. No gate: the ingredients are the only
    // requirement, whether or not the recipe was ever "known".
    const auto exact = crafting::match({"thyme", "water"}, reg);
    REQUIRE(exact.recipe != nullptr);
    REQUIRE(exact.recipe->id == "tea");

    // One ingredient short (just thyme) -> no exact match, nearest = tea at 1/2 closeness.
    const auto close = crafting::match({"thyme"}, reg);
    REQUIRE(close.recipe == nullptr);
    REQUIRE(close.nearest != nullptr);
    REQUIRE(close.nearest->id == "tea");
    REQUIRE(close.closeness == Approx(0.5f));

    // Nothing in common -> no exact, no nearest.
    const auto miss = crafting::match({"stone"}, reg);
    REQUIRE(miss.recipe == nullptr);
    REQUIRE(miss.nearest == nullptr);
}

TEST_CASE("outcomeQuality scales with the craft stat + a bounded luck roll", "[crafting]")
{
    const Recipe r = teaRecipe(); // base 1.0, quality_per_stat default 0.05
    const crafting::Config cfg;

    // Stat 0, min luck -> just the base.
    REQUIRE(crafting::outcomeQuality(r, 0, kZero, cfg) == Approx(1.0f));
    // Stat 10, min luck -> base + 10*0.05.
    REQUIRE(crafting::outcomeQuality(r, 10, kZero, cfg) == Approx(1.0f + 0.5f));
    // Max luck adds up to quality_luck on top.
    REQUIRE(crafting::outcomeQuality(r, 0, kMax, cfg) == Approx(1.0f + cfg.quality_luck));
    // Clamped to quality_max however high the stat.
    REQUIRE(crafting::outcomeQuality(r, 1000, kMax, cfg) == Approx(cfg.quality_max));

    // A permanent output never scales (a key is a key).
    Recipe key = r;
    key.kind = OutputKind::Permanent;
    REQUIRE(crafting::outcomeQuality(key, 1000, kMax, cfg) == Approx(1.0f));
}

TEST_CASE("xpGained runs INVERSE to mastery", "[crafting]")
{
    const Recipe r = teaRecipe(); // xp_base 10
    const crafting::Config cfg;

    // Reaching above your level (good quality, low stat) pays MORE than routine (high stat).
    const int reachXp = crafting::xpGained(r, /*craftStat=*/1, /*quality=*/1.8f, cfg);
    const int routineXp = crafting::xpGained(r, /*craftStat=*/15, /*quality=*/1.0f, cfg);
    REQUIRE(reachXp > routineXp);

    // A mastered craft still pays something (never zero), floored.
    REQUIRE(routineXp >= 1);

    // No XP stat / base -> no XP.
    Recipe plain = r;
    plain.xp_base = 0;
    REQUIRE(crafting::xpGained(plain, 1, 1.5f, cfg) == 0);
}

TEST_CASE("craft consumes inputs, grants output, records discovery + reveal", "[crafting]")
{
    crafting::Registry reg;
    reg.recipes["tea"] = teaRecipe();
    const crafting::Config cfg;
    crafting::State cs;

    inventory::Registry items; // empty defs -> unknown items fall back to cap 1; count() still sums
    inventory::Satchel sat;
    inventory::add(sat, items, inventory::ItemInstance{"thyme", 5});
    inventory::add(sat, items, inventory::ItemInstance{"water", 2});

    const crafting::Outcome out =
        crafting::craft(*reg.find("tea"), sat, items, /*craftStat=*/3, kZero, cfg, cs);

    REQUIRE(out.made);
    REQUIRE(out.output_item == "tea");
    // Inputs consumed at their quantities: 5-2 thyme, 2-1 water.
    REQUIRE(inventory::count(sat, "thyme") == 3);
    REQUIRE(inventory::count(sat, "water") == 1);
    REQUIRE(inventory::count(sat, "tea") == 1);
    // First craft of an unknown recipe: first_time + reveal reported. craft READS known but does
    // NOT insert -- the caller owns marking a recipe learned (so teach-by-deed can't
    // double-insert).
    REQUIRE(out.first_time);
    REQUIRE(out.revealed_flag == "brewed_tea_once");
    REQUIRE(cs.known.empty()); // craft did not mark it known
    REQUIRE(out.stat_gains.size() == 2);
    REQUIRE(out.stat_gains[0].first == "survival");
    REQUIRE(out.xp > 0);

    // The caller learns the recipe (as GameLoop's learnRecipe does), then a re-make is NOT
    // first_time and reports no reveal.
    cs.known.insert("tea");
    const crafting::Outcome again =
        crafting::craft(*reg.find("tea"), sat, items, 3, kZero, cfg, cs);
    REQUIRE(again.made);
    REQUIRE_FALSE(again.first_time);
    REQUIRE(again.revealed_flag.empty());
}

TEST_CASE("craft fails (changes nothing) without enough materials", "[crafting]")
{
    crafting::Registry reg;
    reg.recipes["tea"] = teaRecipe();
    const crafting::Config cfg;
    crafting::State cs;

    inventory::Registry items;
    inventory::Satchel sat;
    inventory::add(sat, items, inventory::ItemInstance{"thyme", 1}); // need 2; no water at all

    const crafting::Outcome out = crafting::craft(*reg.find("tea"), sat, items, 3, kZero, cfg, cs);

    REQUIRE_FALSE(out.made);
    REQUIRE(inventory::count(sat, "thyme") == 1); // unchanged -- all-or-nothing
    REQUIRE(inventory::count(sat, "tea") == 0);
    REQUIRE(cs.known.empty());
}

TEST_CASE("a recipe naming no stats sends the whole reward to the default", "[crafting]")
{
    // Making things is workmanship unless the recipe says otherwise -- an author who omits
    // xp_stats gets the default rather than silently-zero XP.
    Recipe r = teaRecipe();
    r.xp_stats.clear();

    crafting::Config cfg;
    cfg.default_xp_stat = "craftsmanship";
    crafting::State cs;
    inventory::Registry items;
    inventory::Satchel sat;
    inventory::add(sat, items, inventory::ItemInstance{"thyme", 2});
    inventory::add(sat, items, inventory::ItemInstance{"water", 1});

    const crafting::Outcome out = crafting::craft(r, sat, items, /*craftStat=*/1, kZero, cfg, cs);
    REQUIRE(out.made);
    REQUIRE(out.stat_gains.size() == 1);
    REQUIRE(out.stat_gains[0].first == "craftsmanship");
    REQUIRE(out.stat_gains[0].second == out.xp);
}

TEST_CASE("a craft splits its XP across the stats it exercises, by weight", "[crafting]")
{
    // A tool-like make weights workmanship; an organic one weights the field knowledge. The
    // split is by relative weight and always sums to the craft's total.
    Recipe r = teaRecipe();
    r.xp_stats = {crafting::StatShare{"survival", 7.0f},
                  crafting::StatShare{"craftsmanship", 3.0f}};

    const crafting::Config cfg;
    const auto gains = crafting::splitXp(r, 100, cfg);
    REQUIRE(gains.size() == 2);
    REQUIRE(gains[0] == std::pair<std::string, int>{"survival", 70});
    REQUIRE(gains[1] == std::pair<std::string, int>{"craftsmanship", 30});
}

TEST_CASE("splitXp never rounds a real share away, and always sums to the total", "[crafting]")
{
    Recipe r = teaRecipe();
    // 1 XP across two stats, and an awkward 3-way split -- the parts must still sum exactly.
    r.xp_stats = {crafting::StatShare{"survival", 1.0f},
                  crafting::StatShare{"craftsmanship", 1.0f}};
    const crafting::Config cfg;

    auto gains = crafting::splitXp(r, 1, cfg);
    int sum = 0;
    for (const auto& g : gains)
        sum += g.second;
    REQUIRE(sum == 1); // the single point goes somewhere, not nowhere

    r.xp_stats = {crafting::StatShare{"survival", 1.0f}, crafting::StatShare{"craftsmanship", 1.0f},
                  crafting::StatShare{"wonder", 1.0f}};
    gains = crafting::splitXp(r, 10, cfg);
    sum = 0;
    for (const auto& g : gains)
        sum += g.second;
    REQUIRE(sum == 10);
    REQUIRE(gains.size() == 3);
}
