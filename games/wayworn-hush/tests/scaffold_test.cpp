#include "TileMap.h"

#include <catch2/catch_test_macros.hpp>

// Scaffold-stage test: proves the test target links against the engine and
// that this game's 16px world grid resolves cells correctly. Real system tests
// replace/extend this as the vertical slice is built.

TEST_CASE("Wayworn Hush uses a 16px world grid", "[scaffold]")
{
    TileMap tm;
    tm.tile_size = 16;
    tm.width = 8;
    tm.height = 8;
    tm.tiles.assign(64, TileMap::Tile{TileMap::WALKABLE_ID, true});
    tm.placed_rooms.push_back({0, 0, 8, 8});

    // World (40,40) at 16px = cell (2,2), inside the room.
    REQUIRE(tm.findRoomAt(40.0f, 40.0f) == 0);
    // World (200,200) is past the 8*16=128px extent -> outside.
    REQUIRE(tm.findRoomAt(200.0f, 200.0f) == -1);
}
