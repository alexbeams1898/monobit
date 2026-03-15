#include "systems/CameraSystem.h"

#include "ecs/Components.h"

void CameraSystem::update(EntityManager& em)
{
    // The player entity carries Transform + Camera + Input.
    // Snap the camera position to the player's world position each frame.
    // Smooth follow / lerp is a future improvement once core gameplay is stable.
    for (auto [entity, transform, camera, _input] :
         em.registry().view<Transform, Camera, Input>().each())
    {
        if (!camera.active)
            continue;

        camera.x = transform.x;
        camera.y = transform.y;
    }
}
