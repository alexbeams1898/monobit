// Walks the authored Richards thread as the game actually loads it -- the real config files,
// not fixtures -- so a renamed flag or a deleted deed fails here rather than in play.
#include "Arcs.h"
#include "Growth.h"
#include "Psyche.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// The authored world, loaded once per section from the same files the game reads.
struct Authored
{
    psyche::State psyche;
    arcs::Registry threads;
    growth::GrowthState growth;
    std::unordered_set<std::string> observed;
    std::unordered_map<std::string, int> stats;

    Authored()
    {
        psyche::load(psyche, "config/psyche.json", "config/actions.json");
        arcs::load(threads, "config/arcs.json");
        psyche.day = 1;
        psyche.day_frac = 11.0 / 24.0; // mid-morning, inside the trail's window
    }

    std::vector<arcs::Item> agenda()
    {
        return arcs::agenda(threads, psyche::buildKnowledge(psyche, growth, observed, stats));
    }
};
} // namespace

TEST_CASE("mom's line is what puts the trail on the agenda", "[arcs][content]")
{
    Authored w;
    REQUIRE(w.agenda().empty()); // nobody has told him yet

    w.psyche.flags.insert("mom_mentioned_trail");
    const auto list = w.agenda();
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].arc->id == "richards_debris");
    REQUIRE(list[0].openness == arcs::Openness::Open);
}

TEST_CASE("every branch of the morning tells him about the trail", "[arcs][content]")
{
    // The opening forks: he hides (mom_enters -> first_morning at the bedside) or he gets up on
    // his own (mom_calls -> her voice from downstairs). Both scenes set first_morning_done, so
    // whichever fires silences the other -- which means an errand recorded on only one of them
    // is an errand half the players never receive. Every reply on both must set the flag.
    const Authored w;
    int replies = 0;
    for (const auto& id : {"first_morning", "mom_calls"})
    {
        const psyche::Encounter* enc = nullptr;
        for (const auto& e : w.psyche.encounters)
            if (e.id == id)
                enc = &e;
        REQUIRE(enc != nullptr);
        REQUIRE_FALSE(enc->actions.empty());
        for (const auto& a : enc->actions)
        {
            REQUIRE(a.set_flag == "mom_mentioned_trail");
            ++replies;
        }
    }
    REQUIRE(replies >= 3); // two ways to answer at the bedside, one to call back
}

TEST_CASE("the trail's window shuts for the night without failing the thread", "[arcs][content]")
{
    Authored w;
    w.psyche.flags.insert("mom_mentioned_trail");

    w.psyche.day_frac = 6.0 / 24.0; // before he could reasonably knock
    REQUIRE(w.agenda()[0].openness == arcs::Openness::ShutUntil);

    w.psyche.day_frac = 21.0 / 24.0; // after the door shuts
    REQUIRE(w.agenda()[0].openness == arcs::Openness::ShutToday);

    // Still owed the next morning -- sleeping on it loses nothing but face.
    w.psyche.day = 2;
    w.psyche.day_frac = 9.0 / 24.0;
    REQUIRE(w.agenda()[0].openness == arcs::Openness::Open);
}

TEST_CASE("the debris is a real encounter with both halves", "[arcs][content]")
{
    const Authored w;
    const psyche::Encounter* debris = nullptr;
    for (const auto& e : w.psyche.encounters)
        if (e.id == "storm_debris")
            debris = &e;
    REQUIRE(debris != nullptr);
    REQUIRE_FALSE(debris->tiers.empty());   // something to observe
    REQUIRE_FALSE(debris->actions.empty()); // something to do
}

TEST_CASE("clearing the debris closes the thread", "[arcs][content]")
{
    Authored w;
    w.psyche.flags.insert("mom_mentioned_trail");
    REQUIRE(w.agenda().size() == 1);

    w.psyche.flags.insert("richards_debris_cleared");
    REQUIRE(w.agenda().empty());
}

TEST_CASE("the linter reports a thread nothing can finish", "[arcs][content]")
{
    // The map raises the trail's goal flag (the last pile hauled off), not psyche -- so a
    // survey of the CONTENT alone must call the thread unwinnable, and the boot check has to
    // add the map's half before it is honest. This pins both directions.
    const Authored w;
    arcs::Producible world = arcs::survey(w.psyche);
    REQUIRE(world.flags.count("richards_debris_cleared") == 0); // content alone cannot

    const auto contentOnly = arcs::validate(w.threads, world);
    bool sawUnfinishable = false;
    for (const auto& p : contentOnly)
        if (p.detail.find("nothing in the world can finish") != std::string::npos)
            sawUnfinishable = true;
    REQUIRE(sawUnfinishable);

    // With the map's clearing flags folded in, the thread is sound.
    world.flags.insert("richards_debris_cleared");
    for (const auto& p : arcs::validate(w.threads, world))
        REQUIRE(p.detail.find("nothing in the world can finish") == std::string::npos);
}
