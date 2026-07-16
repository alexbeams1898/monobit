#include "PlayerMovement.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_test_macros.hpp>

// canStand is the ONE definition of "a place the player can be" -- movement refuses a
// step with it, and the save's resume point is validated against it (a stale or
// hand-edited save must not strand him off the map). These pin that contract.

namespace
{
// An 8x8 grid of 16px walkable tiles: a 128x128 world with no props.
void makeOpenMap(EntityManager& em)
{
    TileMap& tm = em.tile_map;
    tm.tile_size = 16;
    tm.width = 8;
    tm.height = 8;
    tm.tiles.assign(64, TileMap::Tile{TileMap::WALKABLE_ID, true});
}

// A player-ish entity with a small collider at the origin.
entt::entity makeActor(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{0.0f, 0.0f});
    reg.emplace<Velocity>(e, Velocity{});
    Collider col;
    col.width = 8.0f;
    col.height = 8.0f;
    col.is_solid = true;
    reg.emplace<Collider>(e, col);
    return e;
}
} // namespace

TEST_CASE("canStand accepts open walkable ground", "[movement]")
{
    EntityManager em;
    makeOpenMap(em);
    const entt::entity actor = makeActor(em);

    REQUIRE(player_movement::canStand(em, actor, 64.0f, 64.0f)); // mid-map
}

TEST_CASE("canStand rejects a spot off the map -- the stale-save case", "[movement]")
{
    EntityManager em;
    makeOpenMap(em); // world is 128x128
    const entt::entity actor = makeActor(em);

    // Far outside (the shape of a save written against a bigger/older map).
    REQUIRE_FALSE(player_movement::canStand(em, actor, 999.0f, 777.0f));
    // Just past the edge, and negative -- both are off the world.
    REQUIRE_FALSE(player_movement::canStand(em, actor, 200.0f, 64.0f));
    REQUIRE_FALSE(player_movement::canStand(em, actor, -50.0f, 64.0f));
}

TEST_CASE("canStand rejects non-walkable terrain", "[movement]")
{
    EntityManager em;
    makeOpenMap(em);
    const entt::entity actor = makeActor(em);

    // Make the cell at (4,4) unwalkable (water, say) -- world (64..80, 64..80).
    em.tile_map.tiles[4 * 8 + 4].walkable = false;
    REQUIRE_FALSE(player_movement::canStand(em, actor, 72.0f, 72.0f));
}

TEST_CASE("canStand rejects a spot inside a solid prop", "[movement]")
{
    EntityManager em;
    makeOpenMap(em);
    const entt::entity actor = makeActor(em);

    // A static solid prop (a rock: Collider, no Velocity) sitting mid-map.
    auto& reg = em.registry();
    const entt::entity rock = reg.create();
    reg.emplace<Transform>(rock, Transform{64.0f, 64.0f});
    Collider col;
    col.width = 16.0f;
    col.height = 16.0f;
    col.is_solid = true;
    reg.emplace<Collider>(rock, col);

    REQUIRE_FALSE(player_movement::canStand(em, actor, 64.0f, 64.0f)); // inside it
    REQUIRE(player_movement::canStand(em, actor, 32.0f, 32.0f));       // clear of it
}

TEST_CASE("canStand is false for an entity with no collider", "[movement]")
{
    EntityManager em;
    makeOpenMap(em);
    auto& reg = em.registry();
    const entt::entity bare = reg.create();
    reg.emplace<Transform>(bare, Transform{0.0f, 0.0f});

    REQUIRE_FALSE(player_movement::canStand(em, bare, 64.0f, 64.0f));
}
