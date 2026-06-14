// Per-class flat modifiers (hp_offset etc) per
// [[project_class_stats_v2_locked_2026_06_14]]. Verifies the loader
// + offsetFor lookup: known (class, key) returns the authored
// value; unknown class / unknown key / PlayerClass::None return 0.

#include "AppState.h"
#include "classmods/ClassModifiers.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>

namespace
{

std::string writeStandardConfig(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-classmods-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / (tag + ".json")).string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "hp_offset": {
            "penitent":   50,
            "heretic":    15,
            "ferine":     30,
            "unburdened":  0
        }
    })",
              f);
    std::fclose(f);
    return path;
}

} // namespace

TEST_CASE("Class modifiers return authored value per (key, class)", "[classmods]")
{
    REQUIRE(selva::classmods::loadFromFile(writeStandardConfig("authored")));

    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Penitent, "hp_offset") == 50);
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Heretic, "hp_offset") == 15);
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Ferine, "hp_offset") == 30);
    // Unburdened authored as 0 explicitly; should return 0 (the
    // cosmological refusal-to-install keeps them at the floor).
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Unburdened, "hp_offset") == 0);
}

TEST_CASE("PlayerClass::None contributes 0 (enemies / pre-Signing)", "[classmods]")
{
    REQUIRE(selva::classmods::loadFromFile(writeStandardConfig("none-bypass")));

    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::None, "hp_offset") == 0);
}

TEST_CASE("Unknown modifier key returns 0 (forward-compatible)", "[classmods]")
{
    REQUIRE(selva::classmods::loadFromFile(writeStandardConfig("unknown-key")));

    // Asking for a modifier that doesn't exist (e.g. a future
    // stamina_offset before it's authored) returns 0. Same fail-open
    // shape as soft-caps + identity functions.
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Penitent, "stamina_offset") == 0);
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Heretic, "poise_offset") == 0);
}

TEST_CASE("Class missing from a modifier block returns 0", "[classmods]")
{
    // Config only declares hp_offset for Penitent; other classes
    // aren't in the block.
    namespace fs = std::filesystem;
    const fs::path dir = "tmp/selva-classmods-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / "missing-class.json").string();
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(R"({
        "hp_offset": {
            "penitent": 50
        }
    })",
              f);
    std::fclose(f);
    REQUIRE(selva::classmods::loadFromFile(path));

    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Penitent, "hp_offset") == 50);
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Heretic, "hp_offset") == 0);
    REQUIRE(selva::classmods::offsetFor(selva::PlayerClass::Ferine, "hp_offset") == 0);
}
