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
    life.record = {{"config/pests/ant.json", 31}, {"config/pests/mouse.json", 4}};

    // The basement has no way in, so its own hole is hole zero; the floor below it opens with
    // the far end of that hole, already spent.
    descent::Room basement;
    basement.area = "Bar_B1";
    basement.depth = 0;
    basement.holes = {descent::Hole{descent::Link{1, 0}, "config/holes/foundation_crack.json",
                                    world::Side::North, true, true, 3}};
    descent::Room dug;
    dug.seed = 9182736u;
    dug.depth = 1;
    dug.way_in = 0;
    dug.holes = {descent::Hole{descent::Link{0, 0}, "config/holes/foundation_crack.json",
                               world::Side::South, true, true, 0},
                 descent::Hole{descent::Link{}, "config/holes/gnaw_hole.json", world::Side::East,
                               true, false, 2}};
    life.descent = {basement, dug};

    life.man.chemical = 4;
    life.man.endurance = 7;
    life.man.banked = 120;
    life.man.thermos_fill = 1;
    life.man.thermos_sips = 2;
    life.man.satchel = {savegame::Item{"config/items/husk.json", 2, 5}};
    life.where.room = 1;
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
        REQUIRE(life.record.at("config/pests/ant.json") == 31);
        REQUIRE(life.record.at("config/pests/mouse.json") == 4);
    }

    SECTION("the tree, including which holes are spent and where they lead")
    {
        REQUIRE(life.descent.size() == 2);
        REQUIRE(life.descent[0].area == "Bar_B1");
        REQUIRE(life.descent[0].holes[0].to.room == 1);
        REQUIRE(life.descent[0].holes[0].to.hole == 0);
        REQUIRE(life.descent[0].holes[0].cleared);
        REQUIRE(life.descent[0].holes[0].killed == 3);
        // The wall a hole was cut into cannot be worked out again from the far side, so it has
        // to survive the round trip like the kind does.
        REQUIRE(life.descent[0].holes[0].side == world::Side::North);
        REQUIRE(life.descent[1].holes[0].side == world::Side::South);
        REQUIRE(life.descent[1].holes[1].side == world::Side::East);
        REQUIRE(life.descent[1].seed == 9182736u);
        REQUIRE(life.descent[1].way_in == 0);
        // Both halves of the edge: the way in points back at the hole it is the far end of.
        REQUIRE(life.descent[1].holes[0].to.room == 0);
        REQUIRE(life.descent[1].holes[0].to.hole == 0);
        REQUIRE(life.descent[1].holes[0].cleared);
        REQUIRE_FALSE(life.descent[1].holes[1].cleared);
        REQUIRE(life.descent[1].holes[1].kind == "config/holes/gnaw_hole.json");
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
        REQUIRE(life.where.room == 1);
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
    REQUIRE(back.lives[0].where.room == -1);
    // Never stood anywhere, so a resume must not drop him at the origin.
    REQUIRE_FALSE(back.lives[0].where.stood);
    std::filesystem::remove(path);
}

TEST_CASE("a version 1 floor folds into holes, and the way in becomes one of them")
{
    const std::string path = scratch("poe_save_v1.json");
    // Two rooms as v1 wrote them, VERBATIM -- five parallel per-hole arrays, the way in kept on
    // the far side as from/from_hole rather than as a hole of the room it leads into, and holes
    // living under config/seeps. A fixture written in today's spelling tests nothing: the point
    // of it is that it is out of date.
    std::ofstream(path) << R"({"schema_version": 1, "lives": [{"id": "job-1", "descent": [
        {"area": "Bar_B1", "depth": 0, "child": [1], "cleared": [true], "opened": [true],
         "kind": ["config/seeps/foundation_crack.json"], "killed": [6]},
        {"seed": 7, "depth": 1, "from": 0, "from_hole": 0, "child": [-1, -1],
         "cleared": [true, false], "opened": [true, true],
         "kind": ["config/seeps/gnaw_hole.json", "config/seeps/foundation_crack.json"],
         "killed": [4, 1]}]}]})";
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const std::vector<descent::Room>& tree = back.lives[0].descent;
    REQUIRE(tree.size() == 2);

    SECTION("the first floor has no way in, so its own hole stays hole zero")
    {
        REQUIRE(tree[0].way_in == -1);
        REQUIRE(tree[0].holes.size() == 1);
        REQUIRE(tree[0].holes[0].killed == 6);
        // It leads into the floor below, entered at THAT floor's way in.
        REQUIRE(tree[0].holes[0].to.room == 1);
        REQUIRE(tree[0].holes[0].to.hole == 0);
    }

    SECTION("the dug floor gains the way in as hole zero, already spent")
    {
        REQUIRE(tree[1].way_in == 0);
        REQUIRE(tree[1].holes.size() == 3); // the way in, plus the two it always had
        REQUIRE(tree[1].holes[0].cleared);
        REQUIRE(tree[1].holes[0].opened);
        // It wears the kind of the hole it is the far end of.
        REQUIRE(tree[1].holes[0].kind == "config/holes/foundation_crack.json");
        REQUIRE(tree[1].holes[0].to.room == 0);
        REQUIRE(tree[1].holes[0].to.hole == 0);
    }

    SECTION("its own holes shift up by one, carrying their state with them")
    {
        REQUIRE(tree[1].holes[1].kind == "config/holes/gnaw_hole.json");
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
    descent::Room a;
    a.depth = 3;
    a.label = "B3-A";
    a.way_in = 0;
    a.holes = {descent::Hole{descent::Link{-1, -1}, "config/holes/foundation_crack.json",
                             world::Side::North, true, true, 0},
               descent::Hole{descent::Link{1, 0}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 4},
               descent::Hole{descent::Link{2, 2}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 7}};
    descent::Room b;
    b.depth = 3;
    b.label = "B3-B";
    b.way_in = 0;
    b.holes = {descent::Hole{descent::Link{0, 1}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 0},
               descent::Hole{descent::Link{2, 0}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 2}};
    descent::Room c;
    c.depth = 3;
    c.label = "B3-C";
    c.way_in = 0;
    c.holes = {descent::Hole{descent::Link{1, 1}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 0},
               descent::Hole{descent::Link{}, "config/holes/foundation_crack.json",
                             world::Side::North, false, false, 0},
               descent::Hole{descent::Link{0, 2}, "config/holes/gnaw_hole.json", world::Side::North,
                             true, true, 1}};
    life.descent = {a, b, c};
    life.where.room = 0;
    life.where.stood = true;

    savegame::File file;
    file.lives.push_back(life);
    REQUIRE(savegame::save(file, path));
    // Held by name: at() returns a reference, which breaks the chain that would otherwise keep
    // the loaded File alive, and the tree would be read out of a destroyed object.
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const std::vector<descent::Room>& tree = back.lives.front().descent;
    REQUIRE(tree.size() == 3);

    SECTION("every edge points back at the hole that points to it")
    {
        for (std::size_t f = 0; f < tree.size(); ++f)
            for (std::size_t h = 0; h < tree[f].holes.size(); ++h)
            {
                const descent::Link& to = tree[f].holes[h].to;
                if (to.room < 0)
                    continue;
                INFO("floor " << f << " hole " << h);
                REQUIRE(static_cast<std::size_t>(to.room) < tree.size());
                REQUIRE(static_cast<std::size_t>(to.hole) < tree[to.room].holes.size());
                const descent::Link& back = tree[to.room].holes[to.hole].to;
                CHECK(back.room == static_cast<int>(f));
                CHECK(back.hole == static_cast<int>(h));
            }
    }

    SECTION("the loop is still a loop, and the room it leads back to is still named")
    {
        CHECK(tree[2].holes[2].to.room == 0); // C runs back to A
        CHECK(tree[0].label == "B3-A");
        // A room reached sideways keeps the depth it was opened from -- that is the whole rule.
        CHECK(tree[1].depth == tree[0].depth);
        CHECK(tree[2].depth == tree[0].depth);
    }

    SECTION("a hole that leads nowhere yet is still a question")
    {
        CHECK(tree[2].holes[1].to.room == -1);
        CHECK_FALSE(tree[2].holes[1].cleared);
    }
    std::filesystem::remove(path);
}

// THE FILES MOVED WHEN THE WORDS DID, and a save holds their paths verbatim -- a hole's kind, a
// room's type, and every key of the record, which is keyed BY path. A file written before the
// move points at directories that are gone, and a room whose kind cannot be read builds no
// chambers: it loads as a BLANK MAP rather than as an error, which is the worst way to fail.
TEST_CASE("a save written before the files moved still points at them")
{
    const std::string path = scratch("poe_save_moved.json");
    std::ofstream(path) << R"({"schema_version": 2, "lives": [{"id": "job-1",
        "record": {"config/creatures/ant.json": 12, "config/creatures/mouse.json": 3},
        "descent": [
          {"label": "B1-A", "depth": 1, "type": "config/floors/cellar.json", "way_in": -1,
           "holes": [{"to": {"node": 1, "hole": 0}, "kind": "config/seeps/gnaw_hole.json",
                      "opened": true, "cleared": true, "killed": 5}]},
          {"label": "B1-B", "depth": 1, "type": "config/floors/warren.json", "way_in": 0,
           "holes": [{"to": {"node": 0, "hole": 0}, "kind": "config/seeps/gnaw_hole.json",
                      "opened": true, "cleared": true, "killed": 0},
                     {"kind": "config/seeps/foundation_crack.json"}]}]}]})";
    const savegame::File back = savegame::load(path);
    REQUIRE(back.lives.size() == 1);
    const savegame::Data& life = back.lives.front();

    SECTION("a room's kind points where the file actually is")
    {
        REQUIRE(life.descent.size() == 2);
        CHECK(life.descent[0].type == "config/rooms/cellar.json");
        CHECK(life.descent[1].type == "config/rooms/warren.json");
    }

    SECTION("so does every hole's kind")
    {
        CHECK(life.descent[0].holes[0].kind == "config/holes/gnaw_hole.json");
        CHECK(life.descent[1].holes[1].kind == "config/holes/foundation_crack.json");
    }

    SECTION("and the record, which is keyed BY path -- the tally has to survive the move")
    {
        CHECK(life.record.at("config/pests/ant.json") == 12);
        CHECK(life.record.at("config/pests/mouse.json") == 3);
        CHECK(life.record.count("config/creatures/ant.json") == 0);
    }

    SECTION("the links the earlier fold made are untouched by the move")
    {
        CHECK(life.descent[0].holes[0].to.room == 1);
        CHECK(life.descent[1].holes[0].to.room == 0);
        CHECK(life.descent[1].way_in == 0);
    }
    std::filesystem::remove(path);
}

// A HOLE THAT IS SPENT WAS OPENED, AND WHAT WAS OPENED HAS A TALLY. No version of the game can
// produce a hole marked spent that was never opened and never took anything -- but an earlier
// fold could, by reading a MISSING `opened` array as "none of them" when v1's holes pressed by
// themselves and the field simply did not exist yet. The room then reads as finished the moment
// it is entered, which is silent rather than loud, so it is worth pinning from both ends.
TEST_CASE("a hole cannot be spent without ever having been opened")
{
    SECTION("v1 had no `opened` at all, so every hole in it was open")
    {
        const std::string path = scratch("poe_save_v1_noopened.json");
        std::ofstream(path) << R"({"schema_version": 1, "lives": [{"id": "job-1", "descent": [
            {"seed": 3, "depth": 1, "cleared": [true, false], "killed": [9, 0],
             "kind": ["config/seeps/gnaw_hole.json", "config/seeps/gnaw_hole.json"]}]}]})";
        const savegame::File back = savegame::load(path);
        const std::vector<descent::Room>& tree = back.lives.at(0).descent;
        REQUIRE(tree.size() == 1);
        REQUIRE(tree[0].holes.size() == 2);
        // The spent one stays spent AND comes back opened, which is the only way it could be.
        CHECK(tree[0].holes[0].cleared);
        CHECK(tree[0].holes[0].opened);
        CHECK(tree[0].holes[0].killed == 9);
        // The untouched one is open but unspent -- v1 pressed every hole it built.
        CHECK_FALSE(tree[0].holes[1].cleared);
        std::filesystem::remove(path);
    }

    SECTION("a document already carrying the impossible state is repaired")
    {
        const std::string path = scratch("poe_save_impossible.json");
        std::ofstream(path) << R"({"schema_version": 3, "lives": [{"id": "job-1", "descent": [
            {"label": "B1-E", "depth": 1, "way_in": 0, "holes": [
              {"kind": "config/holes/gnaw_hole.json", "opened": true, "cleared": true, "killed": 0},
              {"kind": "config/holes/foundation_crack.json", "opened": false, "cleared": true, "killed": 0},
              {"kind": "config/holes/foundation_crack.json", "opened": true, "cleared": true, "killed": 7},
              {"kind": "config/holes/foundation_crack.json", "opened": false, "cleared": false, "killed": 0}]}]}]})";
        const savegame::File back = savegame::load(path);
        const std::vector<descent::Room>& tree = back.lives.at(0).descent;
        REQUIRE(tree[0].holes.size() == 4);
        // The way in is spent with no tally BY DESIGN -- it never had a program of its own.
        CHECK(tree[0].holes[0].cleared);
        // Spent, never opened, nothing killed: not a passage. Sealed, as it always was.
        CHECK_FALSE(tree[0].holes[1].cleared);
        // Spent and opened with a tally: a real passage, untouched.
        CHECK(tree[0].holes[2].cleared);
        CHECK(tree[0].holes[2].killed == 7);
        // And one that was never spent stays exactly as it was.
        CHECK_FALSE(tree[0].holes[3].cleared);
        CHECK_FALSE(tree[0].holes[3].opened);
        std::filesystem::remove(path);
    }
}
