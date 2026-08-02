#include "Arcs.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace
{
// A world that can produce two memories, can raise the route flag AND the goal, and gates on
// the goal. The goal belongs in BOTH sets: something must be able to raise it (or the thread
// can never finish) and something must read it (or finishing changes nothing).
arcs::Producible makeWorld()
{
    arcs::Producible w;
    w.observable = {"tide_line", "whelk_case"};
    w.flags = {"cleared_the_reeds", "shore_understood"};
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
    dc.flags = {"cleared_the_reeds"};
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

TEST_CASE("a goal nothing can raise is caught", "[arcs]")
{
    // The worse half of a broken thread: not "finishing changes nothing" but "it can never be
    // finished". An errand the pilgrim would carry for the whole walk.
    arcs::Registry reg;
    reg.arcs.push_back(twoRouteArc());
    arcs::Producible w = makeWorld();
    w.flags.erase("shore_understood");

    const auto problems = arcs::validate(reg, w);
    bool caught = false;
    for (const auto& p : problems)
        if (p.detail.find("never set") != std::string::npos)
            caught = true;
    REQUIRE(caught);
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
    a.routes[1].when.any[0].flags = {"never_set_by_anything"};
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
    psyche::State st;

    psyche::Encounter e;
    e.id = "tide_line";
    e.tiers.resize(2);
    psyche::Action deed;
    deed.id = "clear_it";
    deed.set_flag = "cleared_the_reeds";
    e.actions.push_back(deed);
    unlock::Clause vis;
    vis.flags = {"shore_understood"};
    e.visible_when.any.push_back(vis); // something READS the goal flag
    st.encounters.push_back(e);

    psyche::Thought t;
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

// --- The agenda -----------------------------------------------------------------------

namespace
{
// A held world the Knowledge can point into (Knowledge holds pointers, not copies).
struct Held
{
    std::unordered_set<std::string> observed;
    std::unordered_set<std::string> flags;
    std::unordered_map<std::string, int> stats;

    unlock::Knowledge view() const
    {
        unlock::Knowledge k;
        k.observed = &observed;
        k.flags = &flags;
        k.stats = &stats;
        return k;
    }
};

// An errand he can write down: learned when mom mentions it, done when the flag lands.
arcs::Arc errand()
{
    arcs::Arc a;
    a.id = "richards_debris";
    a.goal_flag = "richards_debris_cleared";
    a.line = "Clear the storm debris from Mr. Richards' trail.";
    unlock::Clause known;
    known.flags = {"mom_mentioned_trail"};
    a.known_when.any.push_back(known);
    return a;
}
} // namespace

TEST_CASE("an arc reaches the agenda only once he has learned it", "[arcs][agenda]")
{
    arcs::Registry reg;
    reg.arcs.push_back(errand());
    Held w;

    REQUIRE(arcs::agenda(reg, w.view()).empty()); // nobody has told him yet

    w.flags.insert("mom_mentioned_trail");
    const auto list = arcs::agenda(reg, w.view());
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].arc->id == "richards_debris");
    REQUIRE(list[0].openness == arcs::Openness::Open); // no window authored = always open
}

TEST_CASE("a finished thread leaves the agenda", "[arcs][agenda]")
{
    arcs::Registry reg;
    reg.arcs.push_back(errand());
    Held w;
    w.flags.insert("mom_mentioned_trail");
    REQUIRE(arcs::agenda(reg, w.view()).size() == 1);

    w.flags.insert("richards_debris_cleared");
    REQUIRE(arcs::agenda(reg, w.view()).empty());
}

TEST_CASE("an arc with no written line stays invisible scaffolding", "[arcs][agenda]")
{
    // The routes-only arcs that exist purely to be linted must never surface to the player.
    arcs::Registry reg;
    reg.arcs.push_back(twoRouteArc());
    Held w;
    REQUIRE(arcs::agenda(reg, w.view()).empty());
}

TEST_CASE("a shut window says when it opens rather than failing the thread", "[arcs][agenda]")
{
    arcs::Registry reg;
    arcs::Arc a = errand();
    unlock::Clause daylight;
    daylight.from = 8.0 / 24.0;
    daylight.to = 20.0 / 24.0;
    a.window.any.push_back(daylight);
    reg.arcs.push_back(a);

    Held w;
    w.flags.insert("mom_mentioned_trail");
    unlock::Knowledge k = w.view();

    k.day_frac = 10.0 / 24.0; // mid-morning: the door is open
    auto list = arcs::agenda(reg, k);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].openness == arcs::Openness::Open);

    k.day_frac = 6.0 / 24.0; // before dawn: shut, but the morning will open it
    list = arcs::agenda(reg, k);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].openness == arcs::Openness::ShutUntil);
    REQUIRE(list[0].opens_at > 7.9 / 24.0);
    REQUIRE(list[0].opens_at < 8.3 / 24.0);

    k.day_frac = 21.0 / 24.0; // after the door shuts: nothing more today
    list = arcs::agenda(reg, k);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].openness == arcs::Openness::ShutToday);
    REQUIRE(list[0].opens_at < 0.0);
}

TEST_CASE("a thread stays on the agenda across days until it is done", "[arcs][agenda]")
{
    // No fail state: sleeping on it does not lose it, which is what lets day-2 content tease
    // him about it (a day_min clause) while the errand is still there to discharge.
    arcs::Registry reg;
    arcs::Arc a = errand();
    unlock::Clause daylight;
    daylight.from = 8.0 / 24.0;
    daylight.to = 20.0 / 24.0;
    a.window.any.push_back(daylight);
    reg.arcs.push_back(a);

    Held w;
    w.flags.insert("mom_mentioned_trail");
    unlock::Knowledge k = w.view();
    k.day = 3;
    k.day_frac = 9.0 / 24.0;

    const auto list = arcs::agenda(reg, k);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].openness == arcs::Openness::Open);
}

TEST_CASE("nextOpening reports nothing for a window already open or absent", "[arcs][agenda]")
{
    Held w;
    unlock::Knowledge k = w.view();
    k.day_frac = 12.0 / 24.0;

    REQUIRE(arcs::nextOpening(unlock::Condition{}, k) < 0.0); // no window at all

    unlock::Condition daylight;
    unlock::Clause c;
    c.from = 8.0 / 24.0;
    c.to = 20.0 / 24.0;
    daylight.any.push_back(c);
    REQUIRE(arcs::nextOpening(daylight, k) < 0.0); // already inside it
}

TEST_CASE("agenda keeps the authored order", "[arcs][agenda]")
{
    arcs::Registry reg;
    arcs::Arc first = errand();
    arcs::Arc second = errand();
    second.id = "brook";
    second.goal_flag = "brook_followed";
    second.line = "Find where the brook goes.";
    reg.arcs.push_back(first);
    reg.arcs.push_back(second);

    Held w;
    w.flags.insert("mom_mentioned_trail");
    const auto list = arcs::agenda(reg, w.view());
    REQUIRE(list.size() == 2);
    REQUIRE(list[0].arc->id == "richards_debris");
    REQUIRE(list[1].arc->id == "brook");
}

TEST_CASE("line, known_when and window parse from the arc file", "[arcs][agenda]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "wayworn_arcs_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "arcs.json";
    {
        std::ofstream(file) << R"({ "arcs": [{
            "id": "richards_debris",
            "goal_flag": "richards_debris_cleared",
            "line": "Clear the storm debris from Mr. Richards' trail.",
            "known_when": [{ "flag": "mom_mentioned_trail" }],
            "window": { "between": ["08:00", "20:00"], "day_min": 1 },
            "routes": [{ "label": "by doing", "unlock_when": [{ "flag": "mom_mentioned_trail" }] }]
        }] })";
    }

    arcs::Registry reg;
    arcs::load(reg, file.string());
    REQUIRE(reg.arcs.size() == 1);
    const arcs::Arc& a = reg.arcs[0];
    REQUIRE(a.line == "Clear the storm debris from Mr. Richards' trail.");
    REQUIRE(a.known_when.any.size() == 1);
    REQUIRE(a.known_when.any[0].flags == std::vector<std::string>{"mom_mentioned_trail"});
    // An object window wraps into the one-clause condition the gate grammar expects.
    REQUIRE(a.window.any.size() == 1);
    REQUIRE(a.window.any[0].from == 8.0 / 24.0);
    REQUIRE(a.window.any[0].to == 20.0 / 24.0);
    REQUIRE(a.window.any[0].day_min == 1);

    // And it behaves as an agenda line once loaded, not just as parsed fields.
    Held w;
    w.flags.insert("mom_mentioned_trail");
    unlock::Knowledge k = w.view();
    k.day = 1;
    k.day_frac = 21.0 / 24.0;
    const auto list = arcs::agenda(reg, k);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].openness == arcs::Openness::ShutToday);

    fs::remove_all(dir);
}
