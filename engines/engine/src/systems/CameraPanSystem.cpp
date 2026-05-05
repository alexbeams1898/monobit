#include "systems/CameraPanSystem.h"

#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

namespace
{

float smoothstep(float t)
{
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void CameraPanSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("CameraPanSystem");

    const float fdt = static_cast<float>(dt);
    auto& reg = em.registry();

    auto view = reg.view<Camera, CameraPan, Transform>();
    for (auto entity : view)
    {
        auto& cam = view.get<Camera>(entity);
        auto& pan = view.get<CameraPan>(entity);
        const auto& transform = view.get<Transform>(entity);

        switch (pan.phase)
        {
        case CameraPan::Phase::ToTarget:
        {
            pan.progress += pan.pan_speed * fdt;
            const float t = smoothstep(pan.progress);
            cam.x = pan.start_x + (pan.target_x - pan.start_x) * t;
            cam.y = pan.start_y + (pan.target_y - pan.start_y) * t;
            if (pan.progress >= 1.0f)
            {
                cam.x = pan.target_x;
                cam.y = pan.target_y;
                pan.progress = 0.0f;
                pan.phase = CameraPan::Phase::Hold;
            }
            break;
        }
        case CameraPan::Phase::Hold:
        {
            pan.progress += fdt / pan.hold_duration;
            if (pan.progress >= 1.0f)
            {
                pan.progress = 0.0f;
                pan.phase = CameraPan::Phase::Return;
            }
            break;
        }
        case CameraPan::Phase::Return:
        {
            pan.progress += pan.pan_speed * fdt;
            const float t = smoothstep(pan.progress);
            cam.x = pan.target_x + (transform.x - pan.target_x) * t;
            cam.y = pan.target_y + (transform.y - pan.target_y) * t;
            if (pan.progress >= 1.0f)
            {
                pan.phase = CameraPan::Phase::Done;
            }
            break;
        }
        case CameraPan::Phase::Done:
        {
            reg.remove<CameraPan>(entity);
            break;
        }
        }
    }
}
