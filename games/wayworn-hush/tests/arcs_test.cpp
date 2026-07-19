#include "Arcs.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{
// A world that can produce two memories and one flag, and gates on the goal.
arcs::Producible makeWorld()
{
    arcs::Producible w;
    w.observable = {"tide_line", "whelk_case"};
    w.flags = {"cleared_the_reeds"};
    w.read_flags = {"shore_understood"};
    return w;
}

// Two routes to one goal: one by looking, one by doing. The shape every arc should have.
arcs::Arc twoRouteArc()
{
    arcs::Arc a;
    a.id = "the_shore";
    a.goal_flag = "shore_understood";

    arcs::Route looking;
    looking.label = "by looking";
    unlock::Clause lc;
    lc.observed = {"tide_line", "whelk_case"};
    lc.stat["perception"] = 3;
    looking.when.any.push_back(lc);

    arcs::Route doing;
    doing.label = "by doing";
    unlock::Clause dc;
    dc.flag = "cleared_the_reeds";
    doing.when.any.push_back(dc);

    a.routes = {looking, doing};
    return a;
}

bool mentions(const std::vector<arcs::Problem>& ps, const std::string& needle)
{
    for (const auto& p : ps)
        if (p.detail.find(needle) != std::string::npos)
            return true;
    return false;
}
} // namespace

TEST_CASE("a well-formed arc reports no problems", "[arcs]")
{
    arcs::Registry reg;
    reg.arcs.push_back(twoRouteArc());
    REQUIRE(arcs::validate(reg, makeWorld()).empty());
}

TEST_CASE("a route requiring a memory nothing produces is caught", "[arcs]")
{
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.routes[0].when.any[0].observed = {"a_thing_that_does_not_exist"};
    reg.arcs.push_back(a);

    const auto problems = arcs::validate(reg, makeWorld());
    REQUIRE_FALSE(problems.empty());
    REQUIRE(mentions(problems, "a_thing_that_does_not_exist"));
}

TEST_CASE("a route requiring a flag nothing sets is caught", "[arcs]")
{
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.routes[1].when.any[0].flag = "never_set_by_anything";
    reg.arcs.push_back(a);

    REQUIRE(mentions(arcs::validate(reg, makeWorld()), "never_set_by_anything"));
}

TEST_CASE("a route stays satisfiable when any one of its clauses can be met", "[arcs]")
{
    // OR-of-ANDs: a broken clause does not doom the route if a sibling clause is reachable.
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    unlock::Clause broken;
    broken.observed = {"does_not_exist"};
    a.routes[0].when.any.insert(a.routes[0].when.any.begin(), broken);
    reg.arcs.push_back(a);

    REQUIRE(arcs::validate(reg, makeWorld()).empty());
}

TEST_CASE("a single-route arc is reported as linear", "[arcs]")
{
    // An arc exists to offer more than one way to the same understanding.
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.routes.pop_back();
    reg.arcs.push_back(a);

    REQUIRE(mentions(arcs::validate(reg, makeWorld()), "linear"));
}

TEST_CASE("a goal flag nothing reads is reported", "[arcs]")
{
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.goal_flag = "nothing_gates_on_this";
    reg.arcs.push_back(a);

    REQUIRE(mentions(arcs::validate(reg, makeWorld()), "never read"));
}

TEST_CASE("an unconditional route is reported (it opens the arc immediately)", "[arcs]")
{
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.routes[1].when.any.clear();
    reg.arcs.push_back(a);

    REQUIRE(mentions(arcs::validate(reg, makeWorld()), "unconditional"));
}

TEST_CASE("duplicate arc ids are reported", "[arcs]")
{
    arcs::Registry reg;
    reg.arcs.push_back(twoRouteArc());
    reg.arcs.push_back(twoRouteArc());

    REQUIRE(mentions(arcs::validate(reg, makeWorld()), "duplicate"));
}

TEST_CASE("a stat requirement never makes a route unreachable", "[arcs]")
{
    // Stats grow, so no stat threshold is ever impossible -- only missing memories and flags
    // can strand a route.
    arcs::Registry reg;
    arcs::Arc a = twoRouteArc();
    a.routes[0].when.any[0].stat["perception"] = 999;
    reg.arcs.push_back(a);

    REQUIRE(arcs::validate(reg, makeWorld()).empty());
}

TEST_CASE("survey reports what the authored content can produce", "[arcs]")
{
    // The bridge the boot-time check runs on: memories from encounters/tiers/thoughts, flags
    // from the deeds and thoughts that set them, and the flags anything gates on.
    observations::State st;

    observations::Encounter e;
    e.id = "tide_line";
    e.tiers.resize(2);
    observations::Action deed;
    deed.id = "clear_it";
    deed.set_flag = "cleared_the_reeds";
    e.actions.push_back(deed);
    unlock::Clause vis;
    vis.flag = "shore_understood";
    e.visible_when.any.push_back(vis); // something READS the goal flag
    st.encounters.push_back(e);

    observations::Thought t;
    t.id = "the_shells_were_a_meal";
    t.set_flag = "midden_understood";
    st.thoughts.push_back(t);

    const arcs::Producible w = arcs::survey(st);

    REQUIRE(w.observable.count("tide_line") == 1);
    REQUIRE(w.observable.count("tide_line@1") == 1); // a reached tier is itself a memory
    REQUIRE(w.observable.count("the_shells_were_a_meal") == 1);
    REQUIRE(w.flags.count("cleared_the_reeds") == 1);
    REQUIRE(w.flags.count("midden_understood") == 1);
    REQUIRE(w.read_flags.count("shore_understood") == 1);
    REQUIRE(w.flags.count("never_authored") == 0);
}
