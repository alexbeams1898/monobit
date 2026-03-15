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
