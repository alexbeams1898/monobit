#include "UnlockCondition.h"

#include <nlohmann/json.hpp>

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
    fl.flags = {"met_hermit"};
    REQUIRE(unlock::clauseHolds(fl, w.view()));
    fl.flags = {"unmet"};
    REQUIRE_FALSE(unlock::clauseHolds(fl, w.view()));

    w.flags.insert("bell_rang");
    fl.flags = {"met_hermit", "bell_rang"}; // a flag series: ALL must be set
    REQUIRE(unlock::clauseHolds(fl, w.view()));
    fl.flags = {"met_hermit", "unmet"};
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
    c.flags = {"met_hermit"};
    c.stat["perception"] = 3;
    REQUIRE(unlock::clauseHolds(c, w.view())); // all three hold

    c.stat["perception"] = 4; // now the stat is too low -> whole clause fails
    REQUIRE_FALSE(unlock::clauseHolds(c, w.view()));
}

TEST_CASE("flag and observed each parse as a string or an array", "[unlock]")
{
    const auto j = nlohmann::json::parse(R"([
        { "flag": "alone" },
        { "flag": ["alarm_off", "tv_off"], "observed": "waking" }
    ])");
    const Condition cond = unlock::parseCondition(j);
    REQUIRE(cond.any.size() == 2);
    REQUIRE(cond.any[0].flags == std::vector<std::string>{"alone"});
    REQUIRE(cond.any[1].flags == std::vector<std::string>{"alarm_off", "tv_off"});
    REQUIRE(cond.any[1].observed == std::vector<std::string>{"waking"});

    World w;
    w.observed.insert("waking");
    w.flags.insert("alarm_off");
    REQUIRE_FALSE(unlock::clauseHolds(cond.any[1], w.view())); // tv_off missing
    w.flags.insert("tv_off");
    REQUIRE(unlock::clauseHolds(cond.any[1], w.view()));
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

TEST_CASE("carrying requires the item; without requires its ABSENCE", "[unlock]")
{
    // Instruments gate content, never truth: a thought forms regardless, but
    // writing it down needs the notebook -- which is authored as clauses, so the
    // "if only I had something to write with" ache is content, not code.
    World w;
    std::unordered_set<std::string> held = {"watch"};
    Knowledge k = w.view();
    k.carrying = &held;

    Clause needsWatch;
    needsWatch.carrying = {"watch"};
    REQUIRE(unlock::clauseHolds(needsWatch, k));

    Clause needsNotebook;
    needsNotebook.carrying = {"notebook"};
    REQUIRE_FALSE(unlock::clauseHolds(needsNotebook, k));

    // `without` is the mirror: it holds only while the thing is NOT held.
    Clause lacksNotebook;
    lacksNotebook.without = {"item:notebook"};
    REQUIRE(unlock::clauseHolds(lacksNotebook, k));
    Clause lacksWatch;
    lacksWatch.without = {"item:watch"};
    REQUIRE_FALSE(unlock::clauseHolds(lacksWatch, k));
}

TEST_CASE("without reads flags, observations and items by prefix", "[unlock]")
{
    World w;
    w.flags.insert("got_up");
    w.observed.insert("bed");
    std::unordered_set<std::string> held = {"notebook"};
    Knowledge k = w.view();
    k.carrying = &held;

    const auto lacks = [&](std::string id)
    {
        Clause c;
        c.without = {std::move(id)};
        return unlock::clauseHolds(c, k);
    };
    REQUIRE_FALSE(lacks("got_up"));      // unprefixed = a flag, and it is set
    REQUIRE_FALSE(lacks("flag:got_up")); // same thing said explicitly
    REQUIRE(lacks("flag:never_happened"));
    REQUIRE_FALSE(lacks("obs:bed")); // he has seen it
    REQUIRE(lacks("obs:river"));
    REQUIRE_FALSE(lacks("item:notebook")); // carried
    REQUIRE(lacks("item:watch"));
}

TEST_CASE("carrying and without parse from JSON, string or array", "[unlock]")
{
    const auto j = nlohmann::json::parse(R"([
        { "carrying": "notebook" },
        { "carrying": ["watch", "notebook"], "without": ["item:lantern", "hid_from_morning"] }
    ])");
    const Condition cond = unlock::parseCondition(j);
    REQUIRE(cond.any[0].carrying == std::vector<std::string>{"notebook"});
    REQUIRE(cond.any[1].carrying == std::vector<std::string>{"watch", "notebook"});
    REQUIRE(cond.any[1].without == std::vector<std::string>{"item:lantern", "hid_from_morning"});
}
