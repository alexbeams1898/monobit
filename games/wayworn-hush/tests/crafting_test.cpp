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
    r.xp_stat = "survival";
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
    REQUIRE(out.xp_stat == "survival");
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
