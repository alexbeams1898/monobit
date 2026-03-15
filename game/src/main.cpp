#include "ConfigLoader.h"
#include "Engine.h"
#include "ecs/Components.h"

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
    ConfigLoader::loadEntity(em, "config/entities/guard.json");

    // Spawn a rectangular wall enclosure around the player (640, 360) and guard (700, 360).
    // Tiles are 32x32. The box runs from x=560 to x=752, y=272 to y=464.
    // Top and bottom rows are 7 tiles wide; left and right columns are 5 tiles tall.
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

    constexpr float LEFT = 560.0f;
    constexpr float RIGHT = 752.0f;
    constexpr float TOP = 272.0f;
    constexpr float BOTTOM = 464.0f;
    constexpr float STEP = 32.0f;
    constexpr int COLS = static_cast<int>((RIGHT - LEFT) / STEP) + 1;
    constexpr int INNER_ROWS = static_cast<int>((BOTTOM - TOP) / STEP) - 1;

    for (int i = 0; i < COLS; ++i) // top row
        spawnWall(LEFT + static_cast<float>(i) * STEP, TOP);
    for (int i = 0; i < COLS; ++i) // bottom row
        spawnWall(LEFT + static_cast<float>(i) * STEP, BOTTOM);
    for (int i = 0; i < INNER_ROWS; ++i) // left column (skip corners)
        spawnWall(LEFT, TOP + static_cast<float>(i + 1) * STEP);
    for (int i = 0; i < INNER_ROWS; ++i) // right column (skip corners)
        spawnWall(RIGHT, TOP + static_cast<float>(i + 1) * STEP);

    if (em.registry().valid(player))
    {
        // Input marks the entity as player-controlled (read by InputSystem).
        // Camera makes this entity the active viewpoint (read by CameraSystem + RenderSystem).
        em.registry().emplace<Input>(player);
        em.registry().emplace<Camera>(player);
    }

    engine.run();
    return 0;
}
