// AuthoredCharacter file-schema round-trip + backward-compat with
// pure-Appearance files. Pins two things:
//   1. save-then-load returns byte-identical fields (up to float
//      precision) -- the schema is stable, no field is silently
//      dropped by the codec.
//   2. Files written before AuthoredCharacter existed
//      (Appearance-only) load into an AuthoredCharacter with
//      default identity fields -- no rewrite required, no crashes.

#include "AppState.h"
#include "gameplay/Appearance.h"
#include "gameplay/AuthoredCharacter.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using selva::gameplay::AuthoredCharacter;
using selva::gameplay::loadAppearance;
using selva::gameplay::loadAuthoredCharacter;
using selva::gameplay::saveAppearance;
using selva::gameplay::saveAuthoredCharacter;

namespace
{

std::filesystem::path tempPath(const char* stem)
{
    // Unique-per-run temp path so parallel test runs don't collide.
    // Cleaned up at the end of each TEST_CASE by RAII.
    static int counter = 0;
    ++counter;
    return std::filesystem::temp_directory_path() /
           (std::string("selva_authored_char_test_") + stem + "_" + std::to_string(counter) +
            ".json");
}

struct ScopedTempFile
{
    std::filesystem::path path;
    ~ScopedTempFile()
    {
        std::filesystem::remove(path);
    }
};

} // namespace

TEST_CASE("AuthoredCharacter round-trip preserves identity fields", "[authored-character][schema]")
{
    const ScopedTempFile tf{tempPath("roundtrip")};

    AuthoredCharacter c;
    c.display_name_key = "interact.npc.guide.display_name";
    c.has_display_name_key = true;
    c.player_class = selva::PlayerClass::None;
    c.has_player_class = true;
    c.stats.str = 5;
    c.stats.dex = 8;
    c.stats.end = 5;
    c.stats.lck = 5;
    c.stats.per = 10;
    c.stats.cog = 10;
    c.stats.intl = 12;
    c.has_stats = true;
    c.rh_item = "shortsword";
    c.has_rh_item = true;
    c.lh_item = "lantern";
    c.has_lh_item = true;
    c.appearance.body_scale = 1.05f;
    c.appearance.head_scale = 0.98f;
    c.appearance.morph_weights["l-eye-trans-in"] = 0.42f;

    REQUIRE(saveAuthoredCharacter(tf.path.string(), c));
    const AuthoredCharacter loaded = loadAuthoredCharacter(tf.path.string());

    REQUIRE(loaded.display_name_key == "interact.npc.guide.display_name");
    REQUIRE(loaded.has_display_name_key);
    REQUIRE(loaded.player_class == selva::PlayerClass::None);
    REQUIRE(loaded.has_player_class);
    REQUIRE(loaded.stats.str == 5);
    REQUIRE(loaded.stats.dex == 8);
    REQUIRE(loaded.stats.per == 10);
    REQUIRE(loaded.stats.intl == 12);
    REQUIRE(loaded.has_stats);
    REQUIRE(loaded.rh_item == "shortsword");
    REQUIRE(loaded.has_rh_item);
    REQUIRE(loaded.lh_item == "lantern");
    REQUIRE(loaded.has_lh_item);
    REQUIRE(loaded.appearance.body_scale == Approx(1.05f));
    REQUIRE(loaded.appearance.head_scale == Approx(0.98f));
    REQUIRE(loaded.appearance.morph_weights.count("l-eye-trans-in") == 1);
    REQUIRE(loaded.appearance.morph_weights.at("l-eye-trans-in") == Approx(0.42f));
}

TEST_CASE("AuthoredCharacter round-trip preserves non-None PlayerClass",
          "[authored-character][schema]")
{
    const ScopedTempFile tf{tempPath("class")};

    AuthoredCharacter c;
    c.player_class = selva::PlayerClass::Penitent;
    c.has_player_class = true;

    REQUIRE(saveAuthoredCharacter(tf.path.string(), c));
    const AuthoredCharacter loaded = loadAuthoredCharacter(tf.path.string());
    REQUIRE(loaded.player_class == selva::PlayerClass::Penitent);
    REQUIRE(loaded.has_player_class);
}

TEST_CASE("AuthoredCharacter loads pure-Appearance files with default identity",
          "[authored-character][backward-compat]")
{
    const ScopedTempFile tf{tempPath("legacy_app")};

    // Write an Appearance-only file the way loadAppearance/saveAppearance
    // would -- no display_name_key / player_class / stats / rh_item / lh_item
    // keys at all.
    selva::gameplay::Appearance app;
    app.body_scale = 1.1f;
    app.morph_weights["chin-vert-out"] = 0.3f;
    REQUIRE(saveAppearance(tf.path.string(), app));

    // Same file loaded through the AuthoredCharacter codec.
    const AuthoredCharacter loaded = loadAuthoredCharacter(tf.path.string());

    REQUIRE(loaded.display_name_key.empty());
    REQUIRE_FALSE(loaded.has_display_name_key);
    REQUIRE(loaded.player_class == selva::PlayerClass::None);
    REQUIRE_FALSE(loaded.has_player_class);
    REQUIRE(loaded.rh_item.empty());
    REQUIRE_FALSE(loaded.has_rh_item);
    REQUIRE(loaded.lh_item.empty());
    REQUIRE_FALSE(loaded.has_lh_item);
    // Stats stay at struct defaults (1s) AND has_stats is false --
    // spawn sites will NOT overlay stats onto the Actor.
    REQUIRE(loaded.stats.str == 1);
    REQUIRE(loaded.stats.intl == 1);
    REQUIRE_FALSE(loaded.has_stats);
    // Appearance keys DID load through.
    REQUIRE(loaded.appearance.body_scale == Approx(1.1f));
    REQUIRE(loaded.appearance.morph_weights.count("chin-vert-out") == 1);
}

TEST_CASE("AuthoredCharacter file is also loadable by legacy loadAppearance",
          "[authored-character][backward-compat]")
{
    // The reverse direction: a file written by AuthoredCharacter must
    // still be readable by loadAppearance -- systems that only care
    // about the Appearance slice (EnemyArchetype's appearance_path
    // consumers today) must not break.
    const ScopedTempFile tf{tempPath("forward_compat")};

    AuthoredCharacter c;
    c.appearance.body_scale = 0.88f;
    c.appearance.morph_weights["forehead-out"] = 0.15f;
    REQUIRE(saveAuthoredCharacter(tf.path.string(), c));

    const selva::gameplay::Appearance legacy = loadAppearance(tf.path.string());
    REQUIRE(legacy.body_scale == Approx(0.88f));
    REQUIRE(legacy.morph_weights.count("forehead-out") == 1);
}

TEST_CASE("loadAuthoredCharacter tolerates a missing file", "[authored-character][safety]")
{
    const AuthoredCharacter c = loadAuthoredCharacter("config/characters/__does_not_exist__.json");
    REQUIRE(c.display_name_key.empty());
    REQUIRE(c.player_class == selva::PlayerClass::None);
    REQUIRE(c.appearance.body_scale == Approx(1.0f));
}
