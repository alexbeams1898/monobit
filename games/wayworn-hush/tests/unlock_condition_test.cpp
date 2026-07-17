#include "UnlockCondition.h"

#include <catch2/catch_test_macros.hpp>

using unlock::Clause;
using unlock::Condition;
using unlock::Knowledge;

namespace
{
// Assemble a Knowledge over some held sets/stats. The sets/map must outlive the
// Knowledge (it holds pointers).
struct World
{
    std::unordered_set<std::string> observed; // observed encounters + fired thoughts
    std::unordered_set<std::string> flags;
    std::unordered_map<std::string, int> stats;

    Knowledge view() const
    {
        Knowledge k;
        k.observed = &observed;
        k.flags = &flags;
        k.stats = &stats;
        return k;
    }
};
} // namespace

TEST_CASE("An empty condition is unconditional (always satisfied)", "[unlock]")
{
    World w;
    REQUIRE(unlock::satisfied(Condition{}, w.view()));
}

TEST_CASE("A clause with no fields set holds trivially", "[unlock]")
{
    World w;
    REQUIRE(unlock::clauseHolds(Clause{}, w.view()));
}

TEST_CASE("observed requires ALL listed memories; flag requires the flag set", "[unlock]")
{
    World w;
    w.observed.insert("water");
    w.observed.insert("flood_plain"); // a fired thought is a memory too
    w.flags.insert("met_hermit");

    Clause one;
    one.observed = {"water"};
    REQUIRE(unlock::clauseHolds(one, w.view()));
    one.observed = {"ridge"};
    REQUIRE_FALSE(unlock::clauseHolds(one, w.view()));

    Clause both; // a series: ALL must be held
    both.observed = {"water", "flood_plain"};
    REQUIRE(unlock::clauseHolds(both, w.view()));
    both.observed = {"water", "ridge"}; // ridge missing -> fails
    REQUIRE_FALSE(unlock::clauseHolds(both, w.view()));

    Clause fl;
    fl.flag = "met_hermit";
    REQUIRE(unlock::clauseHolds(fl, w.view()));
    fl.flag = "unmet";
    REQUIRE_FALSE(unlock::clauseHolds(fl, w.view()));
}

TEST_CASE("stat requires each named stat to be at least its level", "[unlock]")
{
    World w;
    w.stats["perception"] = 3;
    w.stats["body"] = 1;

    Clause c;
    c.stat["perception"] = 3;
    REQUIRE(unlock::clauseHolds(c, w.view()));
    c.stat["body"] = 2; // body is only 1
    REQUIRE_FALSE(unlock::clauseHolds(c, w.view()));
    c.stat["body"] = 1;
    REQUIRE(unlock::clauseHolds(c, w.view()));
    c.stat["missing"] = 1; // absent stat reads as 0
    REQUIRE_FALSE(unlock::clauseHolds(c, w.view()));
}

TEST_CASE("Fields within a clause are AND-ed (all must hold)", "[unlock]")
{
    World w;
    w.observed.insert("water");
    w.flags.insert("met_hermit");
    w.stats["perception"] = 3;

    Clause c;
    c.observed = {"water"};
    c.flag = "met_hermit";
    c.stat["perception"] = 3;
    REQUIRE(unlock::clauseHolds(c, w.view())); // all three hold

    c.stat["perception"] = 4; // now the stat is too low -> whole clause fails
    REQUIRE_FALSE(unlock::clauseHolds(c, w.view()));
}

TEST_CASE("Clauses are OR-ed (any satisfies the condition)", "[unlock]")
{
    World w;
    w.observed.insert("flood_plain"); // a fired thought, held as a memory

    Condition cond;
    Clause a; // path A: needs a stat we don't have
    a.stat["reason"] = 5;
    Clause b; // path B: needs the thought we DO have
    b.observed = {"flood_plain"};
    cond.any = {a, b};

    REQUIRE(unlock::satisfied(cond, w.view())); // B holds

    // Remove it -> neither path holds.
    w.observed.clear();
    REQUIRE_FALSE(unlock::satisfied(cond, w.view()));
}

TEST_CASE("Null knowledge sets read as empty (no crash, nothing held)", "[unlock]")
{
    Knowledge k; // all pointers null
    Clause needsObs;
    needsObs.observed = {"anything"};
    REQUIRE_FALSE(unlock::clauseHolds(needsObs, k));
    Clause needsStat;
    needsStat.stat["perception"] = 1;
    REQUIRE_FALSE(unlock::clauseHolds(needsStat, k));
    REQUIRE(unlock::clauseHolds(Clause{}, k)); // empty clause still trivially true
}
