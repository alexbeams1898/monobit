#include "Observations.h"

#include <catch2/catch_test_macros.hpp>

using observations::Conclusion;
using observations::Observable;
using observations::ObserveResult;
using observations::Outcome;
using observations::State;
using observations::Tier;

namespace
{
// stone at (100,0): tier 0 always; tier 1 requires "water" observed.
// water at (0,0). A conclusion forms once both stone + water are observed.
State makeState()
{
    State s;

    Observable stone;
    stone.id = "stone";
    stone.x = 100;
    stone.y = 0;
    stone.radius = 150; // player at origin (dist 100) is within range
    stone.tiers = {
        Tier{"a stone", {}, 5},
        Tier{"a stone, water-worn", {"water"}, 10},
    };

    Observable water;
    water.id = "water";
    water.x = 0;
    water.y = 100;
    water.radius = 150;
    water.tiers = {Tier{"a dry channel", {}, 5}};

    s.observables = {stone, water};
    s.conclusions = {Conclusion{"lived_here", {"stone", "water"}, "people lived here", 25}};
    return s;
}
} // namespace

TEST_CASE("Observing a faced observable surfaces its tier-0 thought and earns Spirit EXP",
          "[observations]")
{
    State s = makeState();
    // At origin facing +x (east) -> faces the stone at (100,0).
    const ObserveResult r = observations::observe(s, 0, 0, 1, 0);
    REQUIRE(r.outcome == Outcome::NewTier);
    REQUIRE(r.earned == 5);
    REQUIRE(s.pending.size() == 1);
    REQUIRE(s.pending.front() == "a stone");
    REQUIRE(s.observed.at("stone") == 0);
}

TEST_CASE("Facing away from everything observes nothing", "[observations]")
{
    State s = makeState();
    const ObserveResult r = observations::observe(s, 0, 0, -1, 0); // facing west, stone is east
    REQUIRE(r.outcome == Outcome::None);
    REQUIRE(r.earned == 0);
    REQUIRE(s.pending.empty());
}

TEST_CASE("Re-observing the same tier surfaces the thought but earns no Spirit EXP",
          "[observations]")
{
    State s = makeState();
    observations::observe(s, 0, 0, 1, 0); // stone tier 0
    s.pending.clear();
    const ObserveResult r = observations::observe(s, 0, 0, 1, 0); // again, still tier 0
    REQUIRE(r.outcome == Outcome::Reobserved);
    REQUIRE(r.earned == 0);         // nothing new
    REQUIRE(s.pending.size() == 1); // thought still shows
}

TEST_CASE("A deeper tier unlocks once its required observation is made", "[observations]")
{
    State s = makeState();
    // Observe stone first -> tier 0 (water not yet observed).
    observations::observe(s, 0, 0, 1, 0);
    REQUIRE(s.observed.at("stone") == 0);

    // Observe water (at (0,100), face south from origin).
    const ObserveResult w = observations::observe(s, 0, 0, 0, 1);
    REQUIRE(w.outcome == Outcome::Conclusion); // stone+water both observed -> conclusion forms
    s.pending.clear();

    // Re-observe stone: water is known, so tier 1 is now available -> new tier.
    const ObserveResult r = observations::observe(s, 0, 0, 1, 0);
    REQUIRE(r.outcome == Outcome::NewTier);
    REQUIRE(r.earned == 10); // tier-1 reward
    REQUIRE(s.observed.at("stone") == 1);
    REQUIRE(s.pending.front() == "a stone, water-worn");
}

TEST_CASE("A conclusion auto-forms once all its requirements are observed, once only",
          "[observations]")
{
    State s = makeState();
    const ObserveResult stone = observations::observe(s, 0, 0, 1, 0); // stone (5)
    REQUIRE(stone.earned == 5);
    REQUIRE(s.formed.empty()); // water not yet -> no conclusion

    // water (5) + conclusion (25) both land this call -> earned reports the sum.
    const ObserveResult water = observations::observe(s, 0, 0, 0, 1);
    REQUIRE(water.outcome == Outcome::Conclusion);
    REQUIRE(water.earned == 30); // 5 (water tier) + 25 (conclusion)
    REQUIRE(s.formed.count("lived_here") == 1);

    // Re-observing the stone forms no NEW conclusion, but water is now known, so
    // the stone's water-worn tier 1 legitimately unlocks (+10) -- deeper reading
    // on revisit is the point. The conclusion count stays at one.
    s.pending.clear();
    const ObserveResult again = observations::observe(s, 0, 0, 1, 0);
    REQUIRE(again.outcome == Outcome::NewTier);
    REQUIRE(again.earned == 10);
    REQUIRE(s.formed.size() == 1);
}

TEST_CASE("facingObservable reports whether something is faced", "[observations]")
{
    const State s = makeState();
    REQUIRE(observations::facingObservable(s, 0, 0, 1, 0));        // stone east
    REQUIRE(observations::facingObservable(s, 0, 0, 0, 1));        // water south
    REQUIRE_FALSE(observations::facingObservable(s, 0, 0, -1, 0)); // nothing west
}
