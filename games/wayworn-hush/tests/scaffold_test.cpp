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

TEST_CASE("Pixel target scales 384x216 to the window by the largest integer fit", "[scaffold]")
{
    using engine::gl::computeBlitRect;

    // 1080p: 1920/384 = 5, 1080/216 = 5 -> exact x5, no letterbox.
    {
        const auto r = computeBlitRect(384, 216, 1920, 1080);
        REQUIRE(r.scale == 5);
        REQUIRE(r.width == 1920);
        REQUIRE(r.height == 1080);
        REQUIRE(r.x == 0);
        REQUIRE(r.y == 0);
    }

    // 1280x720: 1280/384 = 3, 720/216 = 3 -> x3 = 1152x648, centered with
    // horizontal + vertical letterbox.
    {
        const auto r = computeBlitRect(384, 216, 1280, 720);
        REQUIRE(r.scale == 3);
        REQUIRE(r.width == 1152);
        REQUIRE(r.height == 648);
        REQUIRE(r.x == 64); // (1280 - 1152) / 2
        REQUIRE(r.y == 36); // (720 - 648) / 2
    }

    // The limiting axis wins: a wide-but-short window scales by height.
    {
        const auto r = computeBlitRect(384, 216, 4000, 300);
        REQUIRE(r.scale == 1); // 300/216 = 1 limits; 4000/384 = 10 does not
        REQUIRE(r.height == 216);
    }

    // Window smaller than internal res clamps to scale 1 (never 0 -> vanish).
    {
        const auto r = computeBlitRect(384, 216, 200, 100);
        REQUIRE(r.scale == 1);
    }
}
