#include "AppState.h"
#include "gameplay/Sangue.h"

#include <catch2/catch_test_macros.hpp>

// Sangue currency operations — vessel + lifetime accounting, saturation
// at the cosmological cap, vessel reclamation on death. Pure functions
// over PlayerProfile fields; no engine / SDL / GL needed.

TEST_CASE("grantOnKill adds to both vessel and lifetime", "[sangue]")
{
    selva::PlayerProfile p;
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_lifetime == 0u);

    const auto granted = selva::sangue::grantOnKill(p, 5u);
    REQUIRE(granted == 5u);
    REQUIRE(p.sangue_vessel == 5u);
    REQUIRE(p.sangue_lifetime == 5u);
}

TEST_CASE("grantOnKill accumulates across calls", "[sangue]")
{
    selva::PlayerProfile p;
    selva::sangue::grantOnKill(p, 3u);
    selva::sangue::grantOnKill(p, 7u);
    REQUIRE(p.sangue_vessel == 10u);
    REQUIRE(p.sangue_lifetime == 10u);
}

TEST_CASE("grantOnKill with zero is a no-op", "[sangue]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 42u;
    p.sangue_lifetime = 100u;

    const auto granted = selva::sangue::grantOnKill(p, 0u);
    REQUIRE(granted == 0u);
    REQUIRE(p.sangue_vessel == 42u);
    REQUIRE(p.sangue_lifetime == 100u);
}

TEST_CASE("grantOnKill saturates at the lifetime cap", "[sangue]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = selva::SANGUE_LIFETIME_CAP - 3u;
    p.sangue_lifetime = selva::SANGUE_LIFETIME_CAP - 3u;

    // Grant of 10 should add only 3 to each (the remaining room).
    const auto granted = selva::sangue::grantOnKill(p, 10u);
    REQUIRE(granted == 3u);
    REQUIRE(p.sangue_vessel == selva::SANGUE_LIFETIME_CAP);
    REQUIRE(p.sangue_lifetime == selva::SANGUE_LIFETIME_CAP);

    // Subsequent grants no-op at the cap.
    const auto second = selva::sangue::grantOnKill(p, 100u);
    REQUIRE(second == 0u);
    REQUIRE(p.sangue_vessel == selva::SANGUE_LIFETIME_CAP);
    REQUIRE(p.sangue_lifetime == selva::SANGUE_LIFETIME_CAP);
}

TEST_CASE("vessel and lifetime cap independently", "[sangue]")
{
    // Vessel is full (uncommitted at cap) but lifetime has room: future
    // grants can still tick lifetime by the room remaining there, and
    // the vessel sits at the cap untouched.
    selva::PlayerProfile p;
    p.sangue_vessel = selva::SANGUE_LIFETIME_CAP;
    p.sangue_lifetime = 100u;

    const auto granted = selva::sangue::grantOnKill(p, 10u);
    REQUIRE(granted == 0u);
    REQUIRE(p.sangue_vessel == selva::SANGUE_LIFETIME_CAP);
    REQUIRE(p.sangue_lifetime == 110u);
}

TEST_CASE("reclaimVessel zeroes vessel but preserves lifetime", "[sangue]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 250u;
    p.sangue_lifetime = 1000u;

    selva::sangue::reclaimVessel(p);
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_lifetime == 1000u);
}

TEST_CASE("reclaimVessel on an empty vessel is safe", "[sangue]")
{
    selva::PlayerProfile p;
    selva::sangue::reclaimVessel(p);
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_lifetime == 0u);
}

TEST_CASE("commitVessel is a no-op pre-Beat-4 (player_class=None)", "[sangue][commit]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 100u;
    p.player_class = selva::PlayerClass::None;
    const auto r = selva::sangue::commitVessel(p);
    REQUIRE_FALSE(r.fired);
    REQUIRE(r.amount == 0u);
    REQUIRE(p.sangue_vessel == 100u); // unchanged
}

TEST_CASE("commitVessel is a no-op on empty vessel", "[sangue][commit]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 0u;
    p.player_class = selva::PlayerClass::Penitent;
    const auto r = selva::sangue::commitVessel(p);
    REQUIRE_FALSE(r.fired);
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_riversato == 0u);
}

TEST_CASE("commitVessel for class-picker consumes vessel into substrate",
          "[sangue][commit][crucible]")
{
    // Penitent fires Crucible. Vessel contents are consumed (TBD
    // substrate effect); riversato stays 0. Lifetime untouched
    // (lifetime is the ever-earned ledger; commit doesn't reverse it).
    selva::PlayerProfile p;
    p.sangue_vessel = 250u;
    p.sangue_lifetime = 1000u;
    p.player_class = selva::PlayerClass::Penitent;
    const auto r = selva::sangue::commitVessel(p);
    REQUIRE(r.fired);
    REQUIRE(r.amount == 250u);
    REQUIRE(r.fire_kind == selva::PlayerClass::Penitent);
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_riversato == 0u);
    REQUIRE(p.sangue_lifetime == 1000u);
}

TEST_CASE("commitVessel for unburdened routes vessel into riversato", "[sangue][commit][censer]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 250u;
    p.sangue_riversato = 100u;
    p.player_class = selva::PlayerClass::Unburdened;
    const auto r = selva::sangue::commitVessel(p);
    REQUIRE(r.fired);
    REQUIRE(r.amount == 250u);
    REQUIRE(r.fire_kind == selva::PlayerClass::Unburdened);
    REQUIRE(p.sangue_vessel == 0u);
    REQUIRE(p.sangue_riversato == 350u); // 100 + 250
}

TEST_CASE("commitVessel for Heretic and Ferine also fire Crucible", "[sangue][commit][crucible]")
{
    for (auto c : {selva::PlayerClass::Heretic, selva::PlayerClass::Ferine})
    {
        selva::PlayerProfile p;
        p.sangue_vessel = 50u;
        p.player_class = c;
        const auto r = selva::sangue::commitVessel(p);
        REQUIRE(r.fired);
        REQUIRE(r.fire_kind == c);
        REQUIRE(p.sangue_vessel == 0u);
        REQUIRE(p.sangue_riversato == 0u);
    }
}

TEST_CASE("commitVessel riversato saturates at the lifetime cap", "[sangue][commit][censer][cap]")
{
    selva::PlayerProfile p;
    p.sangue_vessel = 100u;
    p.sangue_riversato = selva::SANGUE_LIFETIME_CAP - 30u;
    p.player_class = selva::PlayerClass::Unburdened;
    selva::sangue::commitVessel(p);
    REQUIRE(p.sangue_riversato == selva::SANGUE_LIFETIME_CAP);
    REQUIRE(p.sangue_vessel == 0u); // vessel still drains fully
}
