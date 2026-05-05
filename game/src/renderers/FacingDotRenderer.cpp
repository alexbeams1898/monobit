#include "renderers/FacingDotRenderer.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace FacingDotRenderer
{

void render(EntityManager& em, int window_w, int window_h, float cam_x, float cam_y, float zoom)
{
    static constexpr float kDotSize = 6.0f;
    static constexpr float kDotOffset = 10.0f;
    const Color color{0.0f, 0.0f, 0.0f, 1.0f};

    const float alpha = em.render_alpha;
    const float snap_cam_x = std::round(cam_x);
    const float snap_cam_y = std::round(cam_y);
    const float half_w = static_cast<float>(window_w) * 0.5f;
    const float half_h = static_cast<float>(window_h) * 0.5f;

    for (auto [entity, transform, facing] : em.registry().view<Transform, FacingDirection>().each())
    {
        if (em.registry().all_of<Animation>(entity) || !em.registry().all_of<Sprite>(entity))
            continue;
        if (facing.aim_override_blend > 0.5f)
            continue;

        float anchor_x = std::round(transform.x);
        float anchor_y = std::round(transform.y);
        if (const auto* prev = em.registry().try_get<PreviousTransform>(entity))
        {
            anchor_x = std::round(prev->x + (transform.x - prev->x) * alpha);
            anchor_y = std::round(prev->y + (transform.y - prev->y) * alpha);
        }

        const float dot_world_x = std::round(anchor_x + facing.render_dx * kDotOffset);
        const float dot_world_y = std::round(anchor_y + facing.render_dy * kDotOffset);

        const float dot_screen_x = (dot_world_x - snap_cam_x) * zoom + half_w;
        const float dot_screen_y = (dot_world_y - snap_cam_y) * zoom + half_h;

        UIRenderer::drawRect(dot_screen_x - kDotSize * 0.5f, dot_screen_y - kDotSize * 0.5f,
                             kDotSize, kDotSize, color);
    }
}

} // namespace FacingDotRenderer
