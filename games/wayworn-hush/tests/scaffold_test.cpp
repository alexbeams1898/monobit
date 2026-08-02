#include "TileMap.h"
#include "gl/PixelRenderTarget.h"

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

TEST_CASE("Pixel target aspect-fits 1280x720 to fill the window", "[scaffold]")
{
    using engine::gl::computeBlitRect;

    // The blit fills the screen at a fractional scale (sharp-bilinear keeps it
    // crisp); only the aspect remainder letterboxes. Exhaustive cases live in the
    // engine's blit_rect_test -- this is the game-side smoke check on its own res.

    // 2048x1152 (16:9 laptop, a non-integer multiple) fills fully -- the case that
    // used to fall to x1 and render tiny.
    {
        const auto r = computeBlitRect(1280, 720, 2048, 1152);
        REQUIRE(r.width == 2048);
        REQUIRE(r.height == 1152);
        REQUIRE(r.x == 0);
        REQUIRE(r.y == 0);
    }

    // 1440p is an exact x2 -- fills edge to edge.
    {
        const auto r = computeBlitRect(1280, 720, 2560, 1440);
        REQUIRE(r.width == 2560);
        REQUIRE(r.height == 1440);
    }
}
