#include "SaveGame.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

// The save's promises: what goes in comes back out, a file that predates a field reads as its
// default rather than failing, and a torn or absent file means "nobody has started" rather than
// an error. (The bridge into the live world needs a window and is covered by playing.)

namespace
{
// A path of our own, so a test never touches the player's actual save.
std::string scratch(const char* name)
{
    const auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p.string();
}

savegame::File oneLife()
{
    savegame::Data life;
    life.id = "job-1";
    life.record = {{"config/creatures/ant.json", 31}, {"config/creatures/mouse.json", 4}};

    descent::Floor basement;
    basement.area = "Bar_B1";
    basement.depth = 0;
    basement.child = {1};
    basement.cleared = {true};
    descent::Floor dug;
    dug.seed = 9182736u;
    dug.depth = 1;
    dug.parent = 0;
    dug.parent_hole = 0;
    dug.child = {-1, -1};
    dug.cleared = {true, false};
    life.descent = {basement, dug};

    life.man.chemical = 4;
    life.man.endurance = 7;
    life.man.banked = 120;
    life.man.thermos_fill = 1;
    life.man.thermos_sips = 2;
    life.man.satchel = {savegame::Item{"config/items/husk.json", 2, 5}};
    life.where.node = 1;
    life.where.area = "";
    life.where.x = 412.5f;
    life.where.y = 208.0f;
    life.where.stood = true;

    savegame::File file;
    file.minted = 1;
    file.lives.push_back(life);
    return file;
}
} // namespace

TEST_CASE("a written job comes back whole")
{
    const std::string path = scratch("poe_save_roundtrip.json");
    REQUIRE(savegame::save(oneLife(), path));
    REQUIRE(savegame::exists(path));

    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const savegame::Data& life = back.lives.front();

    SECTION("the ledger")
    {
        REQUIRE(life.record.at("config/creatures/ant.json") == 31);
        REQUIRE(life.record.at("config/creatures/mouse.json") == 4);
    }

    SECTION("the tree, including which holes are spent and where they lead")
    {
        REQUIRE(life.descent.size() == 2);
        REQUIRE(life.descent[0].area == "Bar_B1");
        REQUIRE(life.descent[0].child == std::vector<int>{1});
        REQUIRE(life.descent[0].cleared[0]);
        REQUIRE(life.descent[1].seed == 9182736u);
        REQUIRE(life.descent[1].parent == 0);
        REQUIRE(life.descent[1].parent_hole == 0);
        REQUIRE(life.descent[1].cleared[0]);
        REQUIRE_FALSE(life.descent[1].cleared[1]);
    }

    SECTION("the man")
    {
        REQUIRE(life.man.chemical == 4);
        REQUIRE(life.man.endurance == 7);
        REQUIRE(life.man.banked == 120);
        REQUIRE(life.man.thermos_sips == 2);
        REQUIRE(life.man.satchel.size() == 1);
        REQUIRE(life.man.satchel[0].id == "config/items/husk.json");
        REQUIRE(life.man.satchel[0].count == 5);
    }

    SECTION("where he stopped -- the floor AND the spot on it")
    {
        REQUIRE(life.where.node == 1);
        REQUIRE(life.where.stood);
        REQUIRE(life.where.x == 412.5f);
        REQUIRE(life.where.y == 208.0f);
    }

    std::filesystem::remove(path);
}

TEST_CASE("nothing to go back to is not an error")
{
    SECTION("no file at all")
    {
        const std::string path = scratch("poe_save_missing.json");
        REQUIRE_FALSE(savegame::exists(path));
        REQUIRE(savegame::load(path).lives.empty());
    }

    SECTION("a file torn in half")
    {
        const std::string path = scratch("poe_save_torn.json");
        std::ofstream(path) << "{\"schema_version\": 1, \"lives\": [{\"id\":";
        REQUIRE_FALSE(savegame::exists(path));
        std::filesystem::remove(path);
    }
}

TEST_CASE("a file written before a field existed reads as its default")
{
    const std::string path = scratch("poe_save_older.json");
    // Everything absent but the identity: the shape must survive its own history.
    std::ofstream(path) << R"({"schema_version": 1, "minted": 1,
                               "lives": [{"id": "job-1"}]})";
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    REQUIRE(back.lives[0].descent.empty());
    REQUIRE(back.lives[0].record.empty());
    REQUIRE(back.lives[0].man.chemical == 1);
    REQUIRE(back.lives[0].where.node == -1);
    // Never stood anywhere, so a resume must not drop him at the origin.
    REQUIRE_FALSE(back.lives[0].where.stood);
    std::filesystem::remove(path);
}

TEST_CASE("a floor answers for every hole it has")
{
    const std::string path = scratch("poe_save_ragged.json");
    // A hand-edited or half-migrated floor whose two lists disagree: the shorter
    // one is filled out rather than left to be read past.
    std::ofstream(path) << R"({"schema_version": 1, "lives": [{"id": "job-1", "descent":
                               [{"seed": 7, "child": [-1, -1, -1], "cleared": [true]}]}]})";
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const descent::Floor& floor = back.lives[0].descent.at(0);
    REQUIRE(floor.child.size() == floor.cleared.size());
    REQUIRE(floor.cleared.size() == 3);
    REQUIRE(floor.cleared[0]);
    REQUIRE_FALSE(floor.cleared[2]);
    std::filesystem::remove(path);
}
