#include "systems/CameraSystem.h"

#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

void CameraSystem::update(EntityManager& em)
{
    ZoneScopedN("CameraSystem");
    // Snap the active camera to its entity's position each frame.
    // Skip entities with CameraPan -- CameraPanSystem owns their position.
    for (auto [entity, transform, camera] : em.registry().view<Transform, Camera>().each())
    {
        if (!camera.active)
            continue;
        if (em.registry().all_of<CameraPan>(entity))
            continue;

        camera.x = transform.x;
        camera.y = transform.y;
    }
}
