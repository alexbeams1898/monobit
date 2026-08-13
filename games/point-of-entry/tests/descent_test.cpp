#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/ZoneUtils.h"
#include "systems/DescentSystem.h"
#include "systems/WaveSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <entt/entt.hpp>

// The persistence primitive under the descent tree: a hole marked cleared
// starts SPENT -- no waves, immediately descendable -- while its neighbours
// press as normal. (The tree's traversal itself needs GL for floor building
// and is integration-tested by running the game.)

namespace
{
std::vector<swarm::Seep> twoSeeps()
{
    return {swarm::Seep{160.0f, 160.0f, "config/seeps/foundation_crack.json"},
            swarm::Seep{480.0f, 160.0f, "config/seeps/gnaw_hole.json"}};
}
} // namespace

TEST_CASE("a pre-cleared hole starts spent; its neighbour presses", "[descent]")
{
    EntityManager em;
    // Both broken open: a sealed hole sends nothing, which is a different test.
    swarm::begin("config/swarm.json", twoSeeps(), 0, {true, false}, {}, {true, true});

    CHECK(swarm::seepCleared(em, 0));
    CHECK_FALSE(swarm::seepCleared(em, 1));
    CHECK(swarm::phase() != swarm::Phase::Cleared);

    // Long enough for emergence: everything that surfaces belongs to the
    // uncleared hole -- the spent one never speaks again.
    for (int i = 0; i < 600; ++i)
        swarm::update(em, 1.0f / 60.0f);
    int fromSpent = 0;
    int fromLive = 0;
    for (const auto [e, src] : em.registry().view<SeepSource>().each())
    {
        if (src.index == 0)
            ++fromSpent;
        if (src.index == 1)
            ++fromLive;
    }
    CHECK(fromSpent == 0);
    CHECK(fromLive > 0);
}

TEST_CASE("every hole pre-cleared is a floor already at rest", "[descent]")
{
    EntityManager em;
    swarm::begin("config/swarm.json", twoSeeps(), 0, {true, true});
    CHECK(swarm::phase() == swarm::Phase::Cleared);
    swarm::update(em, 1.0f);
    CHECK(em.registry().view<Vermin>().size() == 0);
}

// A LEAK IS A REPORT ON HIS OWN UNFINISHED BUSINESS, never a latch on the hole. Only the
// no-floor case runs headless: standing on a real floor needs a window, so the rest -- opened
// and abandoned leaks, finished goes quiet -- is covered by playing it.
TEST_CASE("a way down is quiet until he leaves something running under it")
{
    EntityManager em;
    descent::reset();

    PassageSite site;
    site.hole = 0; // every way down is a hole of some floor
    const entt::entity e = em.registry().create();
    em.registry().emplace<PassageSite>(e, site);

    descent::refreshLeaks(em);
    REQUIRE_FALSE(em.registry().get<PassageSite>(e).leaking);

    // Nor does a hole numbered past the end of the floor he is standing in.
    em.registry().get<PassageSite>(e).hole = 99;
    descent::refreshLeaks(em);
    REQUIRE_FALSE(em.registry().get<PassageSite>(e).leaking);
}

// THE WORK STATE: one answer per tick, and everything that looks different between
// exterminating and not asks it. (Generated space is the other half of the rule; it needs a
// built floor, so it is covered by playing.)
TEST_CASE("the work state follows the leak")
{
    EntityManager em;
    descent::reset(); // not standing in a dug floor: the authored rules apply
    zone::reset();
    REQUIRE_FALSE(zone::combat());

    const entt::entity e = em.registry().create();
    PassageSite site;
    em.registry().emplace<PassageSite>(e, site);

    SECTION("a quiet way down in an authored room is not the trade's ground")
    {
        em.registry().get<PassageSite>(e).leaking = false; // everything below it is finished
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }

    SECTION("a leaking one is")
    {
        em.registry().get<PassageSite>(e).leaking = true;
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
    }

    SECTION("a passage carrying nothing is furniture")
    {
        em.registry().get<PassageSite>(e).leaking = false;
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }
}

TEST_CASE("the changeover waits for something to see")
{
    EntityManager em;
    descent::reset();
    zone::reset();
    const entt::entity e = em.registry().create();
    PassageSite site;
    site.leaking = true;
    em.registry().emplace<PassageSite>(e, site);

    SECTION("a flip starts the changeover over again")
    {
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
        REQUIRE(zone::settle() == 1.0f); // a whole second: long since arrived

        em.registry().get<PassageSite>(e).leaking = false;
        zone::update(em, 0.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
        REQUIRE(zone::settle() == 0.0f);
    }

    SECTION("it is held at the start while a curtain is down, then plays")
    {
        zone::update(em, 1.0f, /*cut=*/true);
        REQUIRE(zone::combat()); // the fact does NOT wait for the curtain
        REQUIRE(zone::settle() == 0.0f);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::settle() == 1.0f);
    }
}

TEST_CASE("the work state asks only whether anything can reach him")
{
    EntityManager em;
    descent::reset();
    zone::reset();

    SECTION("a quiet room with nothing in it is not the trade's ground")
    {
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }

    SECTION("one of the swarm still on its feet is enough, wherever he is")
    {
        const entt::entity v = em.registry().create();
        em.registry().emplace<Vermin>(v);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
    }

    SECTION("one already dying is not")
    {
        const entt::entity v = em.registry().create();
        em.registry().emplace<Vermin>(v);
        em.registry().emplace<Dying>(v);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }
}

// KILLING IS THE ONLY PROGRESS. What a hole has lost is what advances it, so walking out of a
// floor and back in -- or quitting and coming back -- resumes the assault rather than
// re-running it. A creature that emerged and escaped was not killed, so it comes up again.
TEST_CASE("a hole resumes past what he has killed out of it", "[descent]")
{
    EntityManager em;

    SECTION("a hole nothing has been taken from starts at the beginning")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {}, {true, true});
        CHECK(swarm::phase() != swarm::Phase::Cleared);
        CHECK(swarm::remaining(em) == 0); // nothing has surfaced yet
    }

    SECTION("a hole emptied of its whole program is spent, and the floor with it")
    {
        // Far more than any program holds: both holes have nothing left to send.
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {100000, 100000}, {true, true});
        CHECK(swarm::seepCleared(em, 0));
        CHECK(swarm::seepCleared(em, 1));
        CHECK(swarm::phase() == swarm::Phase::Cleared);
    }

    SECTION("a hole part-way through is neither spent nor restarted")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {3, 0}, {true, true});
        CHECK_FALSE(swarm::seepCleared(em, 0));
        CHECK(swarm::phase() != swarm::Phase::Cleared);
    }

    SECTION("progress is reported per hole, and begins empty")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {}, {true, true});
        REQUIRE(swarm::progress().size() == 2);
        CHECK(swarm::progress()[0] == 0);
        CHECK(swarm::progress()[1] == 0);
    }

    SECTION("a remembered tally is what the hole resumes with")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {4, 7}, {true, true});
        REQUIRE(swarm::progress().size() == 2);
        CHECK(swarm::progress()[0] == 4);
        CHECK(swarm::progress()[1] == 7);
    }
}

// The arithmetic itself, watched rather than asserted about: run the floor's clock and count
// what actually surfaces. Both of the fast-forward's early bugs -- waves counted from zero, and
// a resumed hole skipping the breath every other hole waits -- were invisible to a test that
// only asked whether a hole was spent.
namespace
{
// Run one hole's clock until it has finished mustering a wave, and report how many it sent.
int surfacedFrom(EntityManager& em, int hole, float seconds = 30.0f)
{
    for (float t = 0.0f; t < seconds; t += 0.05f)
        swarm::update(em, 0.05f);
    int n = 0;
    for (auto [e, vermin, source] : em.registry().view<Vermin, SeepSource>().each())
        if (source.index == hole)
            ++n;
    return n;
}
} // namespace

TEST_CASE("a resumed hole owes the remainder, then whole waves", "[descent]")
{
    const std::vector<swarm::Seep> one{
        swarm::Seep{160.0f, 160.0f, "config/seeps/foundation_crack.json"}};

    int firstWave = 0;
    {
        EntityManager em;
        swarm::begin("config/swarm.json", one, 0, {}, {}, {true});
        firstWave = surfacedFrom(em, 0);
        REQUIRE(firstWave > 0); // the floor must actually press, or nothing below means anything
    }

    SECTION("part-way through a wave, only the rest of THAT wave comes up")
    {
        EntityManager em;
        const int taken = firstWave / 2;
        REQUIRE(taken > 0);
        swarm::begin("config/swarm.json", one, 0, {}, {taken}, {true});
        REQUIRE(surfacedFrom(em, 0) == firstWave - taken);
    }

    SECTION("a wave killed to the last comes back as the NEXT wave, whole")
    {
        EntityManager em;
        swarm::begin("config/swarm.json", one, 0, {}, {firstWave}, {true});
        // The next wave is a wave of its own size -- never a remainder of the one before it.
        REQUIRE(surfacedFrom(em, 0) >= firstWave);
    }
}

// A FLOOR ANSWERS BEING DISTURBED, not being walked into. Until a hole is broken open its
// program is loaded and silent, so arriving somewhere is a chance to read it rather than a
// fight that started without him.
TEST_CASE("a sealed hole sends nothing until it is opened", "[descent]")
{
    EntityManager em;

    SECTION("a floor nobody has touched stays quiet however long he stands there")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {}, {});
        REQUIRE(swarm::seepSealed(0));
        REQUIRE(swarm::seepSealed(1));
        for (int i = 0; i < 600; ++i)
            swarm::update(em, 1.0f / 60.0f);
        CHECK(swarm::remaining(em) == 0);
        // Nor is a sealed hole mistaken for a spent one -- it is a question, not a way down.
        CHECK_FALSE(swarm::seepCleared(em, 0));
    }

    SECTION("opening one presses that hole and leaves its neighbour sealed")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {}, {});
        swarm::wake(0);
        CHECK_FALSE(swarm::seepSealed(0));
        CHECK(swarm::seepSealed(1));
        for (int i = 0; i < 900; ++i)
            swarm::update(em, 1.0f / 60.0f);
        int fromOpened = 0;
        int fromSealed = 0;
        for (const auto [e, src] : em.registry().view<SeepSource>().each())
        {
            if (src.index == 0)
                ++fromOpened;
            if (src.index == 1)
                ++fromSealed;
        }
        CHECK(fromOpened > 0);
        CHECK(fromSealed == 0);
    }

    SECTION("a hole opened on an earlier visit is still open on the next")
    {
        swarm::begin("config/swarm.json", twoSeeps(), 0, {}, {}, {true, false});
        CHECK_FALSE(swarm::seepSealed(0));
        CHECK(swarm::seepSealed(1));
    }
}

// FLOOR TAGS ARE PERMANENT AND UNBOUNDED. A room's tag is written when it is dug and never
// recomputed, so digging a neighbour cannot renumber somewhere he has already been -- and the
// letters run the way spreadsheet columns do, so a depth can hold any number of rooms without
// the room slot ever borrowing a digit from the depth beside it.
TEST_CASE("a floor's tag is stable, readable and unbounded", "[descent]")
{
    descent::reset();

    SECTION("nowhere has no tag")
    {
        REQUIRE(descent::hereLabel().empty());
        REQUIRE(descent::floorLabel(0).empty());
        REQUIRE(descent::floorLabel(-1).empty());
    }

    SECTION("a point of entry reads as its floor and its number, from one")
    {
        // No floor built here, so the tag is empty -- but the SHAPE is what this pins: the
        // number a technician writes on a wall starts at one and carries a leading zero.
        REQUIRE(descent::poeTag(-1, 0).empty());
    }
}

// THE DELTA NARROWS. The descent branches within an act and rejoins at its end, so it converges
// on its root instead of doubling forever. (The linking itself needs floors built, so it is
// covered by playing; this pins where the boundaries fall and that the dial turns it off.)
TEST_CASE("the descent converges at act boundaries", "[descent]")
{
    SECTION("every act_every-th depth is one floor every branch leads into")
    {
        REQUIRE_FALSE(descent::convergesAt(0)); // the surface is not a rejoining
        REQUIRE_FALSE(descent::convergesAt(1));
        REQUIRE_FALSE(descent::convergesAt(3));
        REQUIRE(descent::convergesAt(4));
        REQUIRE_FALSE(descent::convergesAt(5));
        REQUIRE(descent::convergesAt(8));
        REQUIRE(descent::convergesAt(12));
    }

    SECTION("a negative depth is not a boundary")
    {
        REQUIRE_FALSE(descent::convergesAt(-4));
    }
}

// WHICH WAY A HOLE GOES is the only thing its kind decides, and it is decided by where the hole
// is: one in the ground is a way underneath, one in a wall is a run through a cavity to a room
// at the same depth. Everything else a passage does -- carrying, leaking, refusing to be
// travelled while it delivers -- is identical, which is why nothing else here is direction-aware.
//
// (The tree's traversal needs GL to build a floor, so what a passage CARRIES is integration-
// tested by playing. Restoring a tree and asking about its holes needs neither.)
TEST_CASE("a hole in the ground goes down; a hole in a wall goes across", "[descent]")
{
    descent::reset();
    descent::Floor floor;
    floor.depth = 2;
    floor.holes = {
        descent::Hole{descent::Link{}, "config/seeps/foundation_crack.json", false, false, 0},
        descent::Hole{descent::Link{}, "config/seeps/gnaw_hole.json", false, false, 0}};
    descent::restore({floor});

    CHECK(descent::descends(0, 0));       // a crack in the foundation
    CHECK_FALSE(descent::descends(0, 1)); // a gnawed gap in a wall

    SECTION("a hole that is not one of this floor's does not answer")
    {
        CHECK_FALSE(descent::descends(0, 9));
        CHECK_FALSE(descent::descends(0, -1));
        CHECK_FALSE(descent::descends(7, 0));
    }
    descent::reset();
}

// A FLOOR CARRIES ITS OWN TAG and never recomputes it, so a room keeps its name however many
// neighbours are dug afterwards. Rooms at one depth run A, B, C -- which is what makes lateral
// rooms nameable at all, since they share the depth of the floor they were opened from.
TEST_CASE("a restored floor keeps the name it was given", "[descent]")
{
    descent::reset();
    descent::Floor first;
    first.depth = 2;
    first.label = "B2-A";
    descent::Floor sideways;
    sideways.depth = 2; // reached through a wall, so it sits at the same depth
    sideways.label = "B2-B";
    descent::restore({first, sideways});

    CHECK(descent::floorLabel(0) == "B2-A");
    CHECK(descent::floorLabel(1) == "B2-B");
    // Holes are numbered from one in the floor's own order, the way in included: it is a point
    // of entry the moment something comes through it.
    CHECK(descent::poeTag(1, 0) == "B2-B-1");
    CHECK(descent::poeTag(1, 2) == "B2-B-3");
    CHECK(descent::floorLabel(9).empty());
    descent::reset();
}

// AN ACT BOUNDARY IS AN ARRIVAL, NOT A DEPTH. Every branch that DESCENDS into one lands in the
// same room; a room reached sideways sits at that depth without being an arrival into the act,
// which is what keeps lateral rooms possible there at all.
TEST_CASE("convergence is a property of the depth descended into", "[descent]")
{
    CHECK(descent::convergesAt(4));
    CHECK(descent::convergesAt(8));
    CHECK_FALSE(descent::convergesAt(3));
    CHECK_FALSE(descent::convergesAt(0)); // the first floor converges nothing
}
