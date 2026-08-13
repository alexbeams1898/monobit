#include "formats/SaveGame.h"

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

    // The basement has no way in, so its own hole is hole zero; the floor below it opens with
    // the far end of that hole, already spent.
    descent::Floor basement;
    basement.area = "Bar_B1";
    basement.depth = 0;
    basement.holes = {
        descent::Hole{descent::Link{1, 0}, "config/seeps/foundation_crack.json", true, true, 3}};
    descent::Floor dug;
    dug.seed = 9182736u;
    dug.depth = 1;
    dug.way_in = 0;
    dug.holes = {
        descent::Hole{descent::Link{0, 0}, "config/seeps/foundation_crack.json", true, true, 0},
        descent::Hole{descent::Link{}, "config/seeps/gnaw_hole.json", true, false, 2}};
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
        REQUIRE(life.descent[0].holes[0].to.node == 1);
        REQUIRE(life.descent[0].holes[0].to.hole == 0);
        REQUIRE(life.descent[0].holes[0].cleared);
        REQUIRE(life.descent[0].holes[0].killed == 3);
        REQUIRE(life.descent[1].seed == 9182736u);
        REQUIRE(life.descent[1].way_in == 0);
        // Both halves of the edge: the way in points back at the hole it is the far end of.
        REQUIRE(life.descent[1].holes[0].to.node == 0);
        REQUIRE(life.descent[1].holes[0].to.hole == 0);
        REQUIRE(life.descent[1].holes[0].cleared);
        REQUIRE_FALSE(life.descent[1].holes[1].cleared);
        REQUIRE(life.descent[1].holes[1].kind == "config/seeps/gnaw_hole.json");
        REQUIRE(life.descent[1].holes[1].killed == 2);
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

TEST_CASE("a version 1 floor folds into holes, and the way in becomes one of them")
{
    const std::string path = scratch("poe_save_v1.json");
    // Two floors as v1 wrote them: five parallel per-hole arrays, and the way in kept on the
    // far side as from/from_hole rather than as a hole of the floor it leads into.
    std::ofstream(path) << R"({"schema_version": 1, "lives": [{"id": "job-1", "descent": [
        {"area": "Bar_B1", "depth": 0, "child": [1], "cleared": [true], "opened": [true],
         "kind": ["config/seeps/foundation_crack.json"], "killed": [6]},
        {"seed": 7, "depth": 1, "from": 0, "from_hole": 0, "child": [-1, -1],
         "cleared": [true, false], "opened": [true, true],
         "kind": ["config/seeps/gnaw_hole.json", "config/seeps/foundation_crack.json"],
         "killed": [4, 1]}]}]})";
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const std::vector<descent::Floor>& tree = back.lives[0].descent;
    REQUIRE(tree.size() == 2);

    SECTION("the first floor has no way in, so its own hole stays hole zero")
    {
        REQUIRE(tree[0].way_in == -1);
        REQUIRE(tree[0].holes.size() == 1);
        REQUIRE(tree[0].holes[0].killed == 6);
        // It leads into the floor below, entered at THAT floor's way in.
        REQUIRE(tree[0].holes[0].to.node == 1);
        REQUIRE(tree[0].holes[0].to.hole == 0);
    }

    SECTION("the dug floor gains the way in as hole zero, already spent")
    {
        REQUIRE(tree[1].way_in == 0);
        REQUIRE(tree[1].holes.size() == 3); // the way in, plus the two it always had
        REQUIRE(tree[1].holes[0].cleared);
        REQUIRE(tree[1].holes[0].opened);
        // It wears the kind of the hole it is the far end of.
        REQUIRE(tree[1].holes[0].kind == "config/seeps/foundation_crack.json");
        REQUIRE(tree[1].holes[0].to.node == 0);
        REQUIRE(tree[1].holes[0].to.hole == 0);
    }

    SECTION("its own holes shift up by one, carrying their state with them")
    {
        REQUIRE(tree[1].holes[1].kind == "config/seeps/gnaw_hole.json");
        REQUIRE(tree[1].holes[1].cleared);
        REQUIRE(tree[1].holes[1].killed == 4);
        REQUIRE_FALSE(tree[1].holes[2].cleared);
        REQUIRE(tree[1].holes[2].killed == 1);
    }
    std::filesystem::remove(path);
}

// A NETWORK OF WALL HOLES IS A CYCLE, and a cycle in the persisted tree is new. Both halves of
// every edge have to survive the trip: a connection that came back one-way would let him walk
// into a room he could not walk out of, and one that came back pointing at the wrong hole would
// put him somewhere he never was.
TEST_CASE("a lateral network survives the write with both halves of every edge")
{
    const std::string path = scratch("poe_save_loop.json");
    savegame::Data life;
    life.id = "job-1";

    // Three rooms at one depth: A -> B -> C, and C loops back to A. Every floor's hole 0 is the
    // way it was first entered by; the rest are its own.
    descent::Floor a;
    a.depth = 3;
    a.label = "B3-A";
    a.way_in = 0;
    a.holes = {
        descent::Hole{descent::Link{-1, -1}, "config/seeps/foundation_crack.json", true, true, 0},
        descent::Hole{descent::Link{1, 0}, "config/seeps/gnaw_hole.json", true, true, 4},
        descent::Hole{descent::Link{2, 2}, "config/seeps/gnaw_hole.json", true, true, 7}};
    descent::Floor b;
    b.depth = 3;
    b.label = "B3-B";
    b.way_in = 0;
    b.holes = {descent::Hole{descent::Link{0, 1}, "config/seeps/gnaw_hole.json", true, true, 0},
               descent::Hole{descent::Link{2, 0}, "config/seeps/gnaw_hole.json", true, true, 2}};
    descent::Floor c;
    c.depth = 3;
    c.label = "B3-C";
    c.way_in = 0;
    c.holes = {
        descent::Hole{descent::Link{1, 1}, "config/seeps/gnaw_hole.json", true, true, 0},
        descent::Hole{descent::Link{}, "config/seeps/foundation_crack.json", false, false, 0},
        descent::Hole{descent::Link{0, 2}, "config/seeps/gnaw_hole.json", true, true, 1}};
    life.descent = {a, b, c};
    life.where.node = 0;
    life.where.stood = true;

    savegame::File file;
    file.lives.push_back(life);
    REQUIRE(savegame::save(file, path));
    // Held by name: at() returns a reference, which breaks the chain that would otherwise keep
    // the loaded File alive, and the tree would be read out of a destroyed object.
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const std::vector<descent::Floor>& tree = back.lives.front().descent;
    REQUIRE(tree.size() == 3);

    SECTION("every edge points back at the hole that points to it")
    {
        for (std::size_t f = 0; f < tree.size(); ++f)
            for (std::size_t h = 0; h < tree[f].holes.size(); ++h)
            {
                const descent::Link& to = tree[f].holes[h].to;
                if (to.node < 0)
                    continue;
                INFO("floor " << f << " hole " << h);
                REQUIRE(static_cast<std::size_t>(to.node) < tree.size());
                REQUIRE(static_cast<std::size_t>(to.hole) < tree[to.node].holes.size());
                const descent::Link& back = tree[to.node].holes[to.hole].to;
                CHECK(back.node == static_cast<int>(f));
                CHECK(back.hole == static_cast<int>(h));
            }
    }

    SECTION("the loop is still a loop, and the room it leads back to is still named")
    {
        CHECK(tree[2].holes[2].to.node == 0); // C runs back to A
        CHECK(tree[0].label == "B3-A");
        // A room reached sideways keeps the depth it was opened from -- that is the whole rule.
        CHECK(tree[1].depth == tree[0].depth);
        CHECK(tree[2].depth == tree[0].depth);
    }

    SECTION("a hole that leads nowhere yet is still a question")
    {
        CHECK(tree[2].holes[1].to.node == -1);
        CHECK_FALSE(tree[2].holes[1].cleared);
    }
    std::filesystem::remove(path);
}
