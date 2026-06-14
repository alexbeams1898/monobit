// PlayerClass enum + helpers: cosmological identity locked at Beat 4.
// Per setting.md *The Signing and the commit-fire* + locked
// [[project_crucible_censer_leveling_system]].
//
// Pure-compute tests for the enum / string / path-bit triad. Save/load
// round-trip is covered in save_manager_test.cpp.

#include "AppState.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("PlayerClass default is None (pre-Beat-4)", "[player-class]")
{
    selva::PlayerProfile p;
    REQUIRE(p.player_class == selva::PlayerClass::None);
}

TEST_CASE("playerClassName round-trips through parsePlayerClass", "[player-class]")
{
    const selva::PlayerClass all[] = {
        selva::PlayerClass::None,   selva::PlayerClass::Penitent, selva::PlayerClass::Heretic,
        selva::PlayerClass::Ferine, selva::PlayerClass::Unburdened,
    };
    for (auto c : all)
    {
        const std::string name = selva::playerClassName(c);
        REQUIRE(selva::parsePlayerClass(name) == c);
    }
}

TEST_CASE("parsePlayerClass returns None on unknown / empty", "[player-class]")
{
    REQUIRE(selva::parsePlayerClass("") == selva::PlayerClass::None);
    REQUIRE(selva::parsePlayerClass("penitent") == selva::PlayerClass::None); // case-sensitive
    REQUIRE(selva::parsePlayerClass("UnknownClass") == selva::PlayerClass::None);
}

TEST_CASE("parsePlayerClass back-compat: legacy 'Wretched' maps to Ferine", "[player-class]")
{
    // Pre-2026-06-14 saves stored player_class as "Wretched". The rename
    // doctrine ([[project_class_stats_v2_locked_2026_06_14]]) requires
    // parsePlayerClass to accept both strings so existing characters
    // load with the correct class identity. New saves emit "Ferine".
    REQUIRE(selva::parsePlayerClass("Wretched") == selva::PlayerClass::Ferine);
    REQUIRE(selva::parsePlayerClass("Ferine") == selva::PlayerClass::Ferine);
    // Round-trip via the canonical name is still "Ferine" -- back-compat
    // is one-way (read both, write only the new name).
    REQUIRE(std::string(selva::playerClassName(selva::PlayerClass::Ferine)) == "Ferine");
}

TEST_CASE("isClassPickerPath: only the three signed classes carry chrism-fire", "[player-class]")
{
    // Class-pickers carry the Crucible verb (chrism-fire / feed self).
    REQUIRE(selva::isClassPickerPath(selva::PlayerClass::Penitent));
    REQUIRE(selva::isClassPickerPath(selva::PlayerClass::Heretic));
    REQUIRE(selva::isClassPickerPath(selva::PlayerClass::Ferine));
    // Unburdened carries the Censer verb instead (channel-fire / feed Beatrice).
    REQUIRE_FALSE(selva::isClassPickerPath(selva::PlayerClass::Unburdened));
    // None has no commit verb yet (pre-Beat-4).
    REQUIRE_FALSE(selva::isClassPickerPath(selva::PlayerClass::None));
}
