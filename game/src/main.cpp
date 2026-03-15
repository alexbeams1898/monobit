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

    for (float x = LEFT; x <= RIGHT; x += STEP) // top row
        spawnWall(x, TOP);
    for (float x = LEFT; x <= RIGHT; x += STEP) // bottom row
        spawnWall(x, BOTTOM);
    for (float y = TOP + STEP; y < BOTTOM; y += STEP) // left column (skip corners)
        spawnWall(LEFT, y);
    for (float y = TOP + STEP; y < BOTTOM; y += STEP) // right column (skip corners)
        spawnWall(RIGHT, y);

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
