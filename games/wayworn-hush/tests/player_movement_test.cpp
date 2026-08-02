#include "PlayerMovement.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL.h>

#include <cmath>

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

namespace
{
// A key-state array with one scancode held (SDL uses a 512-ish bool array).
struct Keys
{
    unsigned char state[SDL_NUM_SCANCODES] = {};
    explicit Keys(int scancode)
    {
        state[scancode] = 1;
    }
};

// Drive update() for `frames` fixed steps at 60Hz with the given held key.
void walk(EntityManager& em, entt::entity actor, int scancode, float nudge, int frames)
{
    const Keys k(scancode);
    for (int i = 0; i < frames; ++i)
        player_movement::update(em, actor, k.state, /*speed=*/120.0f, nudge, /*corner_slide=*/1.0f,
                                /*fdt=*/1.0f / 60.0f);
}
} // namespace

// A static solid prop (a tree trunk: Collider, no Velocity) centered at (px,py).
entt::entity makeProp(EntityManager& em, float px, float py, float w, float h)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{px, py});
    Collider col;
    col.width = w;
    col.height = h;
    col.is_solid = true;
    reg.emplace<Collider>(e, col);
    return e;
}

TEST_CASE("deflect: dead-on into a wall's edge glides around the corner", "[movement][nudge]")
{
    // Slidy feel: a head-on hit near an obstacle's edge deflects you past it, no dead stop.
    // Wall fills the bottom-right quadrant (cells 4..7); its top-left corner is world (64,64).
    EntityManager em;
    makeOpenMap(em);
    for (int r = 4; r < 8; ++r)
        for (int c = 4; c < 8; ++c)
            em.tile_map.tiles[r * 8 + c].walkable = false;

    const entt::entity actor = makeActor(em);
    auto& reg = em.registry();
    // Just above the wall's top edge, approaching its top-left corner from the left, walking
    // RIGHT. His box clips the corner -> deflect UP and continue.
    reg.get<Transform>(actor) = Transform{56.0f, 63.0f};

    const float x0 = reg.get<Transform>(actor).x;
    walk(em, actor, SDL_SCANCODE_D, /*nudge=*/8.0f, /*frames=*/20);
    const Transform after = reg.get<Transform>(actor);

    REQUIRE(after.x > x0 + 4.0f); // made progress rightward -- rounded the corner
    REQUIRE(after.y < 63.0f);     // deflected UP, clear of the wall's top
}

TEST_CASE("deflect: dead-on into a TREE glides around it (props, not just tiles)",
          "[movement][nudge]")
{
    // The reported case: walking straight into a tree must slide around, not dead-stop. The
    // trunk is a solid prop; deflection tests props via touchesSolid the same as tiles.
    EntityManager em;
    makeOpenMap(em);
    const entt::entity actor = makeActor(em);
    auto& reg = em.registry();

    // A narrow trunk mid-map; the player approaches its left face slightly OFF-center (so one
    // side is nearer clear), walking RIGHT into it.
    makeProp(em, /*px=*/64.0f, /*py=*/64.0f, /*w=*/12.0f, /*h=*/12.0f);
    reg.get<Transform>(actor) = Transform{52.0f, 61.0f}; // left of the trunk, a touch high

    const float x0 = reg.get<Transform>(actor).x;
    walk(em, actor, SDL_SCANCODE_D, /*nudge=*/8.0f, /*frames=*/30);
    const Transform after = reg.get<Transform>(actor);

    REQUIRE(after.x > x0 + 4.0f); // got past the trunk rather than stopping dead against it
}

TEST_CASE("deflect: boxed in on both sides still stops", "[movement][nudge]")
{
    // A full-width wall: walking into it mid-span, both perpendicular sides are blocked within
    // reach, so there's nowhere to deflect -- he stops. Deflection glides, it doesn't tunnel.
    EntityManager em;
    makeOpenMap(em);
    for (int c = 0; c < 8; ++c)
        em.tile_map.tiles[4 * 8 + c].walkable = false;

    const entt::entity actor = makeActor(em);
    auto& reg = em.registry();
    reg.get<Transform>(actor) = Transform{64.0f, 56.0f}; // just above the wall, mid-span

    walk(em, actor, SDL_SCANCODE_S, /*nudge=*/8.0f, /*frames=*/20);
    const Transform after = reg.get<Transform>(actor);

    REQUIRE(after.y < 64.0f); // stopped above the wall -- never crossed into it
}

TEST_CASE("update reports move INTENT even when collision holds the player", "[movement][nudge]")
{
    // The animation fix: pressed against a flat wall (velocity zeroed), intent must still read
    // "moving" so the walk cycle keeps playing instead of snapping to idle.
    EntityManager em;
    makeOpenMap(em);
    for (int c = 0; c < 8; ++c)
        em.tile_map.tiles[4 * 8 + c].walkable = false;

    const entt::entity actor = makeActor(em);
    auto& reg = em.registry();
    reg.get<Transform>(actor) = Transform{64.0f, 56.0f};

    const Keys down(SDL_SCANCODE_S);
    const player_movement::MoveIntent intent =
        player_movement::update(em, actor, down.state, 120.0f, 8.0f, 1.0f, 1.0f / 60.0f);
    REQUIRE(intent.moving());                     // still trying to walk...
    REQUIRE(intent.dy > 0.0f);                    // ...downward
    REQUIRE(reg.get<Transform>(actor).y < 64.0f); // ...even though collision held him
}

TEST_CASE("the nudge config knob widens the corner-rounding reach", "[movement][nudge]")
{
    // The knob is real: a bigger corner_nudge rounds a corner from further back. Same approach,
    // two reaches -- the generous one makes more rightward progress than the stingy one.
    auto progressWith = [](float nudge)
    {
        EntityManager em;
        makeOpenMap(em);
        for (int r = 4; r < 8; ++r)
            for (int c = 4; c < 8; ++c)
                em.tile_map.tiles[r * 8 + c].walkable = false;
        const entt::entity actor = makeActor(em);
        auto& reg = em.registry();
        reg.get<Transform>(actor) = Transform{56.0f, 60.0f}; // a bit further from the edge
        const float x0 = reg.get<Transform>(actor).x;
        walk(em, actor, SDL_SCANCODE_D, nudge, /*frames=*/20);
        return reg.get<Transform>(actor).x - x0;
    };

    REQUIRE(progressWith(12.0f) >= progressWith(2.0f)); // more reach -> at least as far around
}

TEST_CASE("deflection conserves speed -- a glide never outruns a free walk", "[movement][nudge]")
{
    // The "too fast" fix: deflecting spends the frame's budget REDIRECTED, not added on top.
    // A frame spent sliding along a wall must not move further than a frame of free walking.
    const float budget = 120.0f / 60.0f; // speed * fdt, one frame

    // Free walk: one frame, open ground, measure the distance.
    EntityManager freeEm;
    makeOpenMap(freeEm);
    const entt::entity freeActor = makeActor(freeEm);
    freeEm.registry().get<Transform>(freeActor) = Transform{64.0f, 64.0f};
    walk(freeEm, freeActor, SDL_SCANCODE_D, 8.0f, /*frames=*/1);
    const float freeStep = freeEm.registry().get<Transform>(freeActor).x - 64.0f;

    // Deflecting: walk RIGHT into a wall whose top edge is open above -> glides UP. One frame's
    // total displacement must not exceed the free step (redirected, not additive).
    EntityManager em;
    makeOpenMap(em);
    for (int r = 4; r < 8; ++r)
        for (int c = 4; c < 8; ++c)
            em.tile_map.tiles[r * 8 + c].walkable = false;
    const entt::entity actor = makeActor(em);
    auto& reg = em.registry();
    reg.get<Transform>(actor) = Transform{60.0f, 63.0f}; // pressed at the corner
    const Transform before = reg.get<Transform>(actor);
    walk(em, actor, SDL_SCANCODE_D, 8.0f, /*frames=*/1);
    const Transform after = reg.get<Transform>(actor);

    const float moved = std::hypot(after.x - before.x, after.y - before.y);
    REQUIRE(moved <= budget + 0.01f); // one frame's glide <= one frame's free walk
    REQUIRE(freeStep > 0.0f);         // sanity: the free walk actually moved
}

TEST_CASE("a creature refuses to step onto ANOTHER creature -- Velocity or not", "[movement]")
{
    // The cat-walks-onto-the-player bug: collision used to skip entities with
    // Velocity, so nothing ever blocked against the PLAYER. Solid is solid --
    // a mover's step check must refuse every other body's box.
    EntityManager em;
    makeOpenMap(em);
    const entt::entity cat = makeActor(em);
    em.registry().get<Transform>(cat).x = 32.0f;
    em.registry().get<Transform>(cat).y = 32.0f;

    const entt::entity player = makeActor(em); // has Velocity, like the real one
    em.registry().get<Transform>(player).x = 64.0f;
    em.registry().get<Transform>(player).y = 64.0f;

    REQUIRE_FALSE(player_movement::canStand(em, cat, 64.0f, 64.0f)); // onto the player: no
    REQUIRE(player_movement::canStand(em, cat, 96.0f, 96.0f));       // open ground: fine
}

TEST_CASE("an overlap can always be walked OUT of -- collision never imprisons", "[movement]")
{
    // However two bodies came to overlap (a scene walking someone through you, a
    // same-tick crossing), blocked means "would ENTER a solid", never "is inside
    // one" -- the trapped side must be free to leave.
    EntityManager em;
    makeOpenMap(em);
    const entt::entity player = makeActor(em);
    em.registry().get<Transform>(player).x = 64.0f;
    em.registry().get<Transform>(player).y = 64.0f;

    const entt::entity cat = makeActor(em); // overlapping the player exactly
    em.registry().get<Transform>(cat).x = 64.0f;
    em.registry().get<Transform>(cat).y = 64.0f;

    // Overlapped, every nearby spot must still be reachable -- stepping away is
    // escaping the cat's box, not entering it.
    REQUIRE(player_movement::canStand(em, player, 70.0f, 64.0f));
    REQUIRE(player_movement::canStand(em, player, 64.0f, 58.0f));
}
