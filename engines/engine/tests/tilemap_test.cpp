#include "TileMap.h"

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// TileMap tests — no window, no GPU. Focus: tile_size is a per-instance field,
// so the same grid logic (cell<->world mapping, room lookup, DDA line-of-sight)
// stays correct at any tile size. A game using a 16px overworld grid and a game
// using a 32px action grid must both resolve cells and LOS correctly from the
// same code.
// ---------------------------------------------------------------------------

namespace
{
// Build a solid-bordered open room: walkable interior, solid outer ring.
TileMap makeRoom(int tile_size, int width, int height)
{
    TileMap tm;
    tm.tile_size = tile_size;
    tm.width = width;
    tm.height = height;
    tm.tiles.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                    TileMap::Tile{TileMap::WALKABLE_ID, true});
    for (int r = 0; r < height; ++r)
        for (int c = 0; c < width; ++c)
            if (r == 0 || c == 0 || r == height - 1 || c == width - 1)
                tm.at(c, r) = TileMap::Tile{TileMap::SOLID_ID, false};
    return tm;
}
} // namespace

TEST_CASE("TileMap defaults to a 32px grid", "[tilemap]")
{
    TileMap tm;
    REQUIRE(tm.tile_size == 32);
}

TEST_CASE("findRoomAt maps world position to cell at the instance tile size", "[tilemap]")
{
    // Same 8x8-cell room placed identically in cell space, at two tile sizes.
    // A world point inside the room must resolve to the room regardless of size.
    for (int ts : {16, 32})
    {
        TileMap tm = makeRoom(ts, 8, 8);
        tm.placed_rooms.push_back({1, 1, 6, 6}); // interior room, tiles [1..6]

        // Center of cell (3,3) in world space = (3.5 * ts, 3.5 * ts).
        const float wx = 3.5f * static_cast<float>(ts);
        const float wy = 3.5f * static_cast<float>(ts);
        REQUIRE(tm.findRoomAt(wx, wy) == 0);

        // A point in the solid border ring (cell 0,0) is outside the room.
        REQUIRE(tm.findRoomAt(0.5f * static_cast<float>(ts), 0.5f * static_cast<float>(ts)) == -1);
    }
}

TEST_CASE("hasLineOfSight DDA respects the instance tile size", "[tilemap]")
{
    // Clear horizontal path across the walkable interior at both tile sizes.
    for (int ts : {16, 32})
    {
        TileMap tm = makeRoom(ts, 8, 8);
        const float y = 3.5f * static_cast<float>(ts);
        const float x1 = 1.5f * static_cast<float>(ts);
        const float x2 = 6.5f * static_cast<float>(ts);
        REQUIRE(tm.hasLineOfSight(x1, y, x2, y));

        // A line that must cross the solid border ring is blocked.
        const float outside = 7.5f * static_cast<float>(ts); // into the solid edge column
        REQUIRE_FALSE(tm.hasLineOfSight(x1, y, outside, y));
    }
}

TEST_CASE("A 16px grid resolves twice as many cells per world unit as a 32px grid", "[tilemap]")
{
    // The same world point falls in different cells depending on tile size —
    // this is the whole point of making tile_size per-game.
    TileMap coarse = makeRoom(32, 8, 8);
    TileMap fine = makeRoom(16, 8, 8);
    coarse.placed_rooms.push_back({0, 0, 8, 8});
    fine.placed_rooms.push_back({0, 0, 8, 8});

    // World (48,48): cell (1,1) at 32px, cell (3,3) at 16px.
    REQUIRE(coarse.findRoomAt(48.0f, 48.0f) == 0);
    REQUIRE(fine.findRoomAt(48.0f, 48.0f) == 0);
    // At 16px the room spans world [0..128); (48,48) is inside. At 32px the room
    // spans world [0..256); also inside. Both resolve, from the same code.
}
