#include "ecs/EntityManager.h"
#include "ops/NavUtils.h"

#include <catch2/catch_test_macros.hpp>

// GETTING STUCK IN A WALL IS A LOST SAVE, not an inconvenience: the game writes at every
// stopping point, so a bad position is on disk the instant it happens and comes back faithfully
// on the next launch. Two rules keep it from being reachable and one keeps it from being
// permanent -- these cover the two that live here.
namespace
{
// A room with a solid block in the middle of it. Walls all round, so the edges are solid too.
EntityManager walledRoom()
{
    EntityManager em;
    em.tile_map.tile_size = 32;
    em.tile_map.width = 10;
    em.tile_map.height = 10;
    em.tile_map.tiles.assign(100, TileMap::Tile{0, true});
    const auto at = [&](int c, int r) -> TileMap::Tile&
    { return em.tile_map.tiles[static_cast<std::size_t>(r * 10 + c)]; };
    for (int i = 0; i < 10; ++i)
    {
        at(i, 0).walkable = false;
        at(i, 9).walkable = false;
        at(0, i).walkable = false;
        at(9, i).walkable = false;
    }
    at(5, 5).walkable = false; // the pillar
    return em;
}

// Tile centre in world pixels.
constexpr float mid(int tile)
{
    return static_cast<float>(tile) * 32.0f + 16.0f;
}
} // namespace

TEST_CASE("a spot inside the architecture resolves to one outside it", "[nav]")
{
    const EntityManager em = walledRoom();

    SECTION("a spot that already fits is left exactly where it is")
    {
        float x = mid(3);
        float y = mid(3);
        REQUIRE(world::freeSpotNear(em, x, y, 14.0f, 10.0f));
        CHECK(x == mid(3));
        CHECK(y == mid(3));
    }

    SECTION("a spot inside the pillar comes back standable, and near")
    {
        float x = mid(5);
        float y = mid(5);
        REQUIRE(world::freeSpotNear(em, x, y, 14.0f, 10.0f));
        CHECK(world::boxFree(em, x, y, 14.0f, 10.0f));
        // Nearest, not merely somewhere: the pillar is one tile, so its neighbour is the answer.
        CHECK(std::abs(x - mid(5)) + std::abs(y - mid(5)) <= 32.0f);
    }

    SECTION("a spot inside the outer wall comes back inside the room")
    {
        float x = mid(0);
        float y = mid(0);
        REQUIRE(world::freeSpotNear(em, x, y, 14.0f, 10.0f));
        CHECK(world::boxFree(em, x, y, 14.0f, 10.0f));
    }

    SECTION("a floor with no room in it says so rather than lying")
    {
        EntityManager solid;
        solid.tile_map.tile_size = 32;
        solid.tile_map.width = 6;
        solid.tile_map.height = 6;
        solid.tile_map.tiles.assign(36, TileMap::Tile{0, false});
        float x = mid(3);
        float y = mid(3);
        CHECK_FALSE(world::freeSpotNear(solid, x, y, 14.0f, 10.0f));
    }
}

// The rule answers "may I enter that space", which assumes the space being LEFT is one you were
// allowed to be in. Where that is false it used to refuse every direction at once -- so anything
// that got inside a wall could never get out, in any direction, ever.
TEST_CASE("movement can climb out of solid ground", "[nav]")
{
    const EntityManager em = walledRoom();
    const float w = 14.0f;
    const float h = 10.0f;

    SECTION("from open ground the rule is unchanged")
    {
        float x = mid(3);
        CHECK(world::stepBlocked(em, x, 4.0f, /*horizontal=*/true, mid(3), w, h));
        CHECK(x == mid(3) + 4.0f);

        // Into the pillar is still refused: this is not a licence to walk through walls.
        float into = mid(4) + 8.0f;
        CHECK_FALSE(world::stepBlocked(em, into, 12.0f, /*horizontal=*/true, mid(5), w, h));
        CHECK(into == mid(4) + 8.0f);
    }

    SECTION("from inside the pillar, some direction moves him")
    {
        int moved = 0;
        for (const float step : {8.0f, -8.0f})
        {
            float x = mid(5);
            if (world::stepBlocked(em, x, step, /*horizontal=*/true, mid(5), w, h))
                ++moved;
            float y = mid(5);
            if (world::stepBlocked(em, y, step, /*horizontal=*/false, mid(5), w, h))
                ++moved;
        }
        CHECK(moved > 0); // the whole point: not every direction is refused
    }

    SECTION("but not deeper in")
    {
        // Standing in the outer wall's corner, the move that buries him further is still refused
        // while the one toward the room is allowed.
        float out = mid(0);
        CHECK(world::stepBlocked(em, out, 16.0f, /*horizontal=*/true, mid(1), w, h));
        float deeper = mid(1);
        CHECK_FALSE(world::stepBlocked(em, deeper, -16.0f, /*horizontal=*/false, mid(0), w, h));
    }
}

// A HOLE IN A WALL NEEDS A WALL. The stubs of solid standing inside a room -- a divider, a
// pillar, the rock a corridor was cut past -- have the same floor on their other side, so a run
// through one goes nowhere. What separates them from a room's boundary is BODY: the boundary has
// the rock the room was carved out of behind it.
TEST_CASE("an arch only takes a wall with body behind it", "[nav]")
{
    EntityManager em;
    em.tile_map.tile_size = 32;
    em.tile_map.width = 8;
    em.tile_map.height = 8;
    em.tile_map.tiles.assign(64, TileMap::Tile{0, true});
    const auto solid = [&](int c, int r)
    { em.tile_map.tiles[static_cast<std::size_t>(r * 8 + c)].walkable = false; };

    // A boundary at the top: two rows of rock, the way a carved room backs onto the mass.
    for (int c = 0; c < 8; ++c)
    {
        solid(c, 0);
        solid(c, 1);
    }
    // A one-tile divider across the middle, with open floor on both sides of it.
    for (int c = 2; c < 6; ++c)
        solid(c, 4);

    SECTION("the boundary is a wall, and its bottom edge comes back")
    {
        const float face = world::wallFaceAbove(em, mid(3), mid(3), 4, 2);
        REQUIRE(face >= 0.0f);
        CHECK(face == 64.0f); // the bottom edge of row 1
    }

    SECTION("the divider is not, however close it is")
    {
        CHECK(world::wallFaceAbove(em, mid(3), mid(5), 4, 2) < 0.0f);
    }

    SECTION("asking for no body at all takes the divider, which is the old behaviour")
    {
        CHECK(world::wallFaceAbove(em, mid(3), mid(5), 4, 1) >= 0.0f);
    }

    SECTION("open floor above is no wall at any depth")
    {
        CHECK(world::wallFaceAbove(em, mid(3), mid(7), 1, 2) < 0.0f);
    }
}

// A HOLE THAT ARCHES INTO A WALL HAS TO BE AT ONE. Where its position is inherited rather than
// chosen -- the way in wears the kind of the hole it is the far end of -- the spot is searched
// for instead, because the floor's own entrance is picked for standing room and knows nothing
// about what is above it.
TEST_CASE("a spot for an arch is found at a wall, not merely on floor", "[nav]")
{
    EntityManager em;
    em.tile_map.tile_size = 32;
    em.tile_map.width = 10;
    em.tile_map.height = 10;
    em.tile_map.tiles.assign(100, TileMap::Tile{0, true});
    const auto solid = [&](int c, int r)
    { em.tile_map.tiles[static_cast<std::size_t>(r * 10 + c)].walkable = false; };
    // Rock across the top two rows; the rest of the map is open.
    for (int c = 0; c < 10; ++c)
    {
        solid(c, 0);
        solid(c, 1);
    }

    SECTION("an open spot far from any wall is moved to one")
    {
        float x = mid(5);
        float y = mid(8);
        REQUIRE(world::archSpotNear(em, x, y, 1.0f, 1.0f, /*reach=*/4, /*depth=*/2));
        CHECK(world::wallFaceAbove(em, x, y, 4, 2) >= 0.0f);
        CHECK(world::boxFree(em, x, y, 1.0f, 1.0f));
    }

    SECTION("a spot that already has a wall over it is left alone")
    {
        float x = mid(5);
        float y = mid(2);
        REQUIRE(world::archSpotNear(em, x, y, 1.0f, 1.0f, 4, 2));
        CHECK(x == mid(5));
        CHECK(y == mid(2));
    }

    SECTION("a map with no wall at all says so rather than lying")
    {
        EntityManager open;
        open.tile_map.tile_size = 32;
        open.tile_map.width = 6;
        open.tile_map.height = 6;
        open.tile_map.tiles.assign(36, TileMap::Tile{0, true});
        float x = mid(3);
        float y = mid(3);
        // Off the map is solid but has nothing behind it, so no face qualifies at this depth.
        CHECK_FALSE(world::archSpotNear(open, x, y, 1.0f, 1.0f, /*reach=*/1, /*depth=*/3));
    }
}
