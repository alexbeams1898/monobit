#include "Ambience.h"

#include <catch2/catch_test_macros.hpp>

// Audio playback needs a device (integration-tested by running the game); these
// cover the flag-bus BOOKKEEPING -- what fires when -- which is pure state.
// AudioSystem is uninitialized here, so start/stop calls are safe no-ops.

namespace
{
ambience::Config withClunk()
{
    ambience::Config cfg;
    ambience::Channel clunk;
    clunk.path = "assets/audio/tv_off.ogg";
    clunk.start_on_flag = "tv_off";
    cfg.channels["tv_off_clunk"] = clunk;
    return cfg;
}
} // namespace

TEST_CASE("a start_on_flag channel fires once when its flag appears", "[ambience]")
{
    const ambience::Config cfg = withClunk();
    ambience::State st;
    std::unordered_set<std::string> flags;

    ambience::tick(st, cfg, flags);
    REQUIRE(st.flag_started.empty()); // no flag yet -- nothing fires

    flags.insert("tv_off");
    ambience::tick(st, cfg, flags);
    REQUIRE(st.flag_started.count("tv_off_clunk") == 1); // the flag landed -- fired
}

TEST_CASE("arm() marks already-held flags as fired so a resume never replays them", "[ambience]")
{
    // The saved walk turned the TV off long ago: the flag is restored as HELD.
    // Without arming, the first tick would read "held + not yet fired" and play
    // the shutdown clunk at boot -- the restore masquerading as the event.
    const ambience::Config cfg = withClunk();
    const std::unordered_set<std::string> flags = {"tv_off"};

    ambience::State st;
    ambience::arm(st, cfg, flags);
    REQUIRE(st.flag_started.count("tv_off_clunk") == 1); // armed: counts as fired
    REQUIRE(st.playing.empty());                         // ...but nothing SOUNDED
}
