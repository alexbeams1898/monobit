// Identity-stat derivation tests per [[project_identity_stats_derived_erasure_locked_2026_06_14]].
// Verifies the foundation: pre-signing returns 0; signing anchors +1 in
// the committed class only; the value re-evaluates under any class lens
// the test asks for (the Erasure-as-lens-swap behavior).

#include "AppState.h"
#include "identity/Identity.h"

#include <cstdio>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace
{

// Write a known-good identity_functions.json to a tmp path and load
// it. Same shape as the shipped config but isolated so test ordering
// can't pollute other tests' formula state. Returns the path.
std::string writeAnchorOnlyConfig(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-identity-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / (tag + ".json")).string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "penitent":   { "signing_anchor": 1.0 },
        "heretic":    { "signing_anchor": 1.0 },
        "ferine":     { "signing_anchor": 1.0 },
        "unburdened": { "signing_anchor": 1.0 }
    })",
               f);
    std::fclose(f);
    return path;
}

} // namespace

TEST_CASE("identity stat is 0 pre-Signing (no class committed)", "[identity]")
{
    REQUIRE(selva::identity::loadFromFile(writeAnchorOnlyConfig("pre-signing")));

    selva::PlayerProfile p;
    // Default profile: player_class = None, flags = {} -- no
    // signing_committed.
    REQUIRE(p.player_class == selva::PlayerClass::None);

    // Every class evaluates to 0 because signing_anchor reads
    // signing_committed && current-class==this-class, and the flag
    // is absent.
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Penitent) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Heretic) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Ferine) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Unburdened) == 0);
    // None always returns 0 regardless of ledger.
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::None) == 0);
}

TEST_CASE("Signing as Penitent anchors +1 Penance only", "[identity]")
{
    REQUIRE(selva::identity::loadFromFile(writeAnchorOnlyConfig("signing-penitent")));

    selva::PlayerProfile p;
    p.player_class = selva::PlayerClass::Penitent;
    p.flags.push_back("signing_committed");

    // The Penitent lens reads "signing_committed && class==Penitent"
    // == true -> +1.
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Penitent) == 1);
    // Every other class's lens reads "signing_committed && class==X"
    // where X != Penitent -> 0. The Vagrant did sign, but he signed
    // as a Penitent; nothing in the ledger reads as a Heretic /
    // Ferine / Unburdened act yet.
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Heretic) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Ferine) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Unburdened) == 0);
}

TEST_CASE("Each Signing anchors +1 in the committed class's lens only", "[identity][class-cycle]")
{
    REQUIRE(selva::identity::loadFromFile(writeAnchorOnlyConfig("signing-cycle")));

    const selva::PlayerClass picker_classes[] = {
        selva::PlayerClass::Penitent,
        selva::PlayerClass::Heretic,
        selva::PlayerClass::Ferine,
        selva::PlayerClass::Unburdened,
    };

    for (auto committed : picker_classes)
    {
        selva::PlayerProfile p;
        p.player_class = committed;
        p.flags.push_back("signing_committed");

        for (auto lens : picker_classes)
        {
            const int v = selva::identity::computeIdentityStat(p, lens);
            const int expected = (lens == committed) ? 1 : 0;
            REQUIRE(v == expected);
        }
    }
}

TEST_CASE("Erasure lens-swap: same ledger, different lens, different result", "[identity][erasure]")
{
    REQUIRE(selva::identity::loadFromFile(writeAnchorOnlyConfig("erasure-swap")));

    // Penitent run: signed, no other actions yet.
    selva::PlayerProfile p;
    p.player_class = selva::PlayerClass::Penitent;
    p.flags.push_back("signing_committed");

    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Penitent) == 1);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Heretic) == 0);

    // Erasure to Heretic: player_class changes; ledger (flags etc.)
    // is unchanged. The Penitent lens now reads 0 (current class is
    // no longer Penitent), and the Heretic lens reads 1 (the
    // signing_committed flag now anchors as Heretic). This IS the
    // Erasure-as-lens-swap behavior: no conversion math; just
    // reading the same ledger through a different lens.
    p.player_class = selva::PlayerClass::Heretic;
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Penitent) == 0);
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Heretic) == 1);
}

TEST_CASE("computeIdentityStat returns 0 when no function loaded for the class", "[identity]")
{
    // Write a config missing one class entry; computeIdentityStat
    // for that class returns 0 quietly rather than crashing.
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-identity-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / "missing-class.json").string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "penitent": { "signing_anchor": 1.0 }
    })",
               f);
    std::fclose(f);
    REQUIRE(selva::identity::loadFromFile(path));

    selva::PlayerProfile p;
    p.player_class = selva::PlayerClass::Heretic;
    p.flags.push_back("signing_committed");

    // Heretic function isn't loaded -- result is 0 (not garbage).
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Heretic) == 0);
    // Penitent function IS loaded; reading through that lens against
    // a Heretic-committed profile returns 0 because the anchor
    // checks current class == Penitent.
    REQUIRE(selva::identity::computeIdentityStat(p, selva::PlayerClass::Penitent) == 0);
}
