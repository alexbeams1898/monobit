#include "systems/CameraSystem.h"

#include "ecs/Components.h"

void CameraSystem::update(EntityManager& em)
{
    // Snap the active camera to its entity's position each frame.
    for (auto [entity, transform, camera] : em.registry().view<Transform, Camera>().each())
    {
        if (!camera.active)
            continue;

        camera.x = transform.x;
        camera.y = transform.y;
    }
}
