#include "Loot.h"

#include <catch2/catch_test_macros.hpp>

using loot::Entry;
using loot::Table;

namespace
{
// A table of two herbs (common) + one stone (rarer), 1-3 rolls.
Table herbTable()
{
    Table t;
    t.id = "herbs";
    t.rolls_min = 1;
    t.rolls_max = 3;
    t.entries = {Entry{"wild_thyme", 80, 1, 2}, Entry{"river_stone", 20, 1, 1}};
    return t;
}

// rng(n) -> 0 for every call: picks the low end of every range (first roll count, first
// entry, min qty). Deterministic worst-case-low.
const observations::RollRng kZero = [](int) { return 0; };

// rng(n) -> n: the high end of every range (max roll count, last entry, max qty).
const observations::RollRng kMax = [](int n) { return n; };
} // namespace

TEST_CASE("An empty / zero-weight table rolls nothing", "[loot]")
{
    Table empty;
    empty.id = "empty";
    REQUIRE(loot::roll(empty, kMax).empty());

    Table zeroed;
    zeroed.id = "zeroed";
    zeroed.entries = {Entry{"x", 0, 1, 1}}; // weight 0 -> filtered from total
    REQUIRE(zeroed.totalWeight() == 0);
    REQUIRE(loot::roll(zeroed, kMax).empty());
}

TEST_CASE("rng->0 takes the low end: one roll, first entry, min quantity", "[loot]")
{
    const auto out = loot::roll(herbTable(), kZero);
    REQUIRE(out.size() == 1);           // rolls_min
    REQUIRE(out[0].id == "wild_thyme"); // first entry (cumulative weight hits it first)
    REQUIRE(out[0].quantity == 1);      // qty_min
}

TEST_CASE("rng->n takes the high end: max rolls, last entry, max quantity", "[loot]")
{
    const auto out = loot::roll(herbTable(), kMax);
    REQUIRE(out.size() == 3); // rolls_max
    for (const auto& inst : out)
    {
        // rng(total-1) = total-1 = 99 lands past thyme's 80 -> the last entry (stone).
        REQUIRE(inst.id == "river_stone");
        REQUIRE(inst.quantity == 1); // stone qty is fixed 1..1
    }
}

TEST_CASE("Every rolled item stays within its entry's quantity bounds", "[loot]")
{
    // Sweep a range of rng responses; thyme qty must always land in [1,2], stone in [1,1].
    for (int k = 0; k < 8; ++k)
    {
        const observations::RollRng rng = [k](int n) { return n < k ? n : k; };
        for (const auto& inst : loot::roll(herbTable(), rng))
        {
            if (inst.id == "wild_thyme")
                REQUIRE((inst.quantity >= 1 && inst.quantity <= 2));
            else if (inst.id == "river_stone")
                REQUIRE(inst.quantity == 1);
        }
    }
}

TEST_CASE("The roll count always lands within rolls_min..rolls_max", "[loot]")
{
    // The rng contract is: rng(n) returns a value in [0,n]. A well-behaved rng clamped to
    // that range must yield a roll count inside the table's bounds.
    const Table t = herbTable();
    for (int k = 0; k <= 5; ++k)
    {
        const observations::RollRng rng = [k](int n) { return k <= n ? k : n; };
        const auto out = loot::roll(t, rng);
        REQUIRE(out.size() >= static_cast<std::size_t>(t.rolls_min));
        REQUIRE(out.size() <= static_cast<std::size_t>(t.rolls_max));
    }
}

TEST_CASE("A weighted pick lands on the entry whose cumulative band contains the roll", "[loot]")
{
    // Single roll (rolls 1..1), controlled weighted index: rng returns a fixed value for the
    // weight pick. thyme occupies band [0,79], stone [80,99].
    Table t = herbTable();
    t.rolls_min = 1;
    t.rolls_max = 1;

    auto pickAt = [&t](int weightRoll)
    {
        // With rolls_min==rolls_max the roll-count skips rng entirely, so the FIRST rng call
        // is the weighted-index pick over [0,99]; the second is qty. Return weightRoll for
        // the index call and 0 for qty (min).
        int call = 0;
        const observations::RollRng rng = [&call, weightRoll](int)
        { return call++ == 0 ? weightRoll : 0; };
        return loot::roll(t, rng);
    };

    REQUIRE(pickAt(0).at(0).id == "wild_thyme");   // start of thyme's band
    REQUIRE(pickAt(79).at(0).id == "wild_thyme");  // last of thyme's band
    REQUIRE(pickAt(80).at(0).id == "river_stone"); // start of stone's band
    REQUIRE(pickAt(99).at(0).id == "river_stone"); // last of stone's band
}
