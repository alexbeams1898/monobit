#include "ConfigLoader.h"
#include "Engine.h"
#include "ecs/Components.h"
#include "systems/SpawnerSystem.h"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Engine engine;

    if (!engine.init("Prison Break", 1280, 720))
        return 1;

    // Load the player entity from config, then attach an Input component so
    // InputSystem treats it as player-controlled.
    // ConfigLoader returns entt::null on failure — guard before emplacing.
    auto& em = engine.entityManager();
    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    SpawnerSystem::load(em, "config/spawns/initial_spawn.json");

    // Spawn a rectangular wall enclosure centred on (640, 360).
    // Tiles are 32x32. Box: x=[320,960], y=[128,576] — 20 tiles wide, 14 tiles tall.
    // TOP=128 ensures (BOTTOM-TOP)=448 is an exact multiple of STEP=32, so the
    // side columns connect flush with both the top and bottom rows (no gaps).
    // A 2-tile door opening is left in the centre of the bottom wall.
    auto spawnWall = [&](float x, float y)
    {
        auto wall = ConfigLoader::loadEntity(em, "config/entities/wall.json");
        if (em.registry().valid(wall))
        {
            auto& t = em.registry().get<Transform>(wall);
            t.x = x;
            t.y = y;
        }
    };

    constexpr float LEFT = 320.0f;
    constexpr float RIGHT = 960.0f;
    constexpr float TOP = 128.0f;
    constexpr float BOTTOM = 576.0f;
    constexpr float STEP = 32.0f;
    constexpr int COLS = static_cast<int>((RIGHT - LEFT) / STEP) + 1;
    constexpr int INNER_ROWS = static_cast<int>((BOTTOM - TOP) / STEP) - 1;

    // Door opening: skip 4 tiles on the bottom wall, centred at x=640.
    // 4 tiles = 128 px gap — gives 48 px clearance on each side for a 32 px
    // entity so the flow-field path never runs along the edge of the opening.
    constexpr int DOOR_IDX_A = COLS / 2 - 1; // i=9  → x=608
    constexpr int DOOR_IDX_B = COLS / 2;     // i=10 → x=640
    constexpr int DOOR_IDX_C = COLS / 2 + 1; // i=11 → x=672
    constexpr int DOOR_IDX_D = COLS / 2 + 2; // i=12 → x=704

    for (int i = 0; i < COLS; ++i) // top row
        spawnWall(LEFT + static_cast<float>(i) * STEP, TOP);
    for (int i = 0; i < COLS; ++i) // bottom row — 4-tile door gap at centre
    {
        if (i == DOOR_IDX_A || i == DOOR_IDX_B || i == DOOR_IDX_C || i == DOOR_IDX_D)
            continue;
        spawnWall(LEFT + static_cast<float>(i) * STEP, BOTTOM);
    }
    for (int i = 0; i < INNER_ROWS; ++i) // left column (skip corners)
        spawnWall(LEFT, TOP + static_cast<float>(i + 1) * STEP);
    for (int i = 0; i < INNER_ROWS; ++i) // right column (skip corners)
        spawnWall(RIGHT, TOP + static_cast<float>(i + 1) * STEP);

    // Helpers for spawning axis-aligned wall runs.
    // Int loop counters avoid clang-analyzer-security.FloatLoopCounter.
    auto spawnWallH = [&](float xStart, int count, float y)
    {
        for (int i = 0; i < count; ++i)
            spawnWall(xStart + static_cast<float>(i) * STEP, y);
    };
    auto spawnWallV = [&](float x, float yStart, int count)
    {
        for (int i = 0; i < count; ++i)
            spawnWall(x, yStart + static_cast<float>(i) * STEP);
    };

    // --- South corridor ------------------------------------------------------
    // Extends the door jambs southward 5 tiles — a chokepoint before the open
    // southern zone.  Tests flow-field narrow-passage routing.
    spawnWallV(576.0f, BOTTOM + STEP, 5); // left wall  y=608..736
    spawnWallV(736.0f, BOTTOM + STEP, 5); // right wall y=608..736

    // --- East L-barrier ------------------------------------------------------
    // Asymmetric L-shape east of the room.  Forces enemies to route around a
    // convex corner — good test of diagonal BFS at a non-axis-aligned obstacle.
    spawnWallH(1024.0f, 5, 288.0f); // horizontal: x=1024..1152, y=288
    spawnWallV(1024.0f, 320.0f, 3); // vertical drop: x=1024, y=320..384

    // --- West pillar field ---------------------------------------------------
    // Two staggered columns of single-tile pillars.  Open enough to feel like
    // a VS-style arena; breaks sight lines and tests flow-field around isolated
    // obstacles.  Col A at x=160 on even rows, col B at x=256 on odd rows.
    for (int i = 0; i < 4; ++i)
    {
        spawnWall(160.0f, 224.0f + static_cast<float>(i) * STEP * 2);
        spawnWall(256.0f, 256.0f + static_cast<float>(i) * STEP * 2);
    }

    if (em.registry().valid(player))
    {
        // Input marks the entity as player-controlled (read by InputSystem).
        // Camera is initialised at the player's spawn position so the first
        // render frame is correct — without this the camera starts at (0,0)
        // and snaps on frame 2, causing a visible world-jump on startup.
        em.registry().emplace<Input>(player);
        const auto& pt = em.registry().get<Transform>(player);
        em.registry().emplace<Camera>(player, Camera{pt.x, pt.y, true});
    }

    engine.run();
    return 0;
}
