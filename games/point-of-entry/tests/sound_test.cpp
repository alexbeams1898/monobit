#include "ops/SoundOps.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

// THE REAL BANK IS LOADED HERE, not a fixture, because the fault this pins was in reading the
// document rather than in any one entry: a range bound to a call ON a temporary iterates an
// object that has already been destroyed. That reads as garbage only sometimes -- it survived
// two launches and killed the third -- so nothing but exercising the real path catches it.
namespace
{
std::string scratch(const char* name)
{
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p.string();
}
} // namespace

TEST_CASE("the game's own sound bank loads", "[sound]")
{
    REQUIRE(sound::load("config/audio.json"));
    // Playing is a no-op without an audio device, which is what the test machine is: the point
    // is that asking for a name neither throws nor kills anything.
    sound::play("footstep");
    sound::play("nothing-is-called-this");
}

TEST_CASE("a bank that is missing or broken leaves the game silent, not dead", "[sound]")
{
    SECTION("no file at all")
    {
        CHECK_FALSE(sound::load("config/there-is-no-bank.json"));
        sound::play("footstep");
    }

    SECTION("a file that is not JSON")
    {
        const std::string path = scratch("poe_bank_bad.json");
        std::ofstream(path) << "{ this is not json";
        CHECK_FALSE(sound::load(path));
        std::filesystem::remove(path);
    }

    SECTION("an entry naming no file is dropped, and the rest of the bank still loads")
    {
        const std::string path = scratch("poe_bank_partial.json");
        std::ofstream(path) << R"({"sounds": {
            "empty": {"volume": 1.0},
            "fine":  {"variations": ["assets/audio/footstep_walk_1.ogg"]}}})";
        CHECK(sound::load(path));
        sound::play("fine");
        sound::play("empty");
        std::filesystem::remove(path);
    }

    SECTION("one path instead of a list is a bank of one")
    {
        const std::string path = scratch("poe_bank_single.json");
        std::ofstream(path) << R"({"sounds": {"tap": {"path": "assets/audio/wand_dry.ogg"}}})";
        CHECK(sound::load(path));
        sound::play("tap");
        std::filesystem::remove(path);
    }
}
