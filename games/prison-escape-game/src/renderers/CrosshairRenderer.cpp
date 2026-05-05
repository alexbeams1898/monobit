#include "renderers/CrosshairRenderer.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL.h>
#include <cmath>

namespace CrosshairRenderer
{

void render(EntityManager& em, int window_w, int window_h, float cam_x, float cam_y, float zoom)
{
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    float cross_x = static_cast<float>(mx);
    float cross_y = static_cast<float>(my);

    // Lock-on blend: aim_override_x/y are world-space, mouse is screen-space.
    // Convert the override target to screen-space using the same camera transform
    // RenderSystem uses for sprites, then lerp.
    const float snap_cam_x = std::round(cam_x);
    const float snap_cam_y = std::round(cam_y);
    const float half_w = static_cast<float>(window_w) * 0.5f;
    const float half_h = static_cast<float>(window_h) * 0.5f;
    for (auto [entity, facing] : em.registry().view<FacingDirection>().each())
    {
        if (facing.aim_override_blend > 0.0f)
        {
            const float override_screen_x = (facing.aim_override_x - snap_cam_x) * zoom + half_w;
            const float override_screen_y = (facing.aim_override_y - snap_cam_y) * zoom + half_h;
            const float b = facing.aim_override_blend;
            cross_x = cross_x + (override_screen_x - cross_x) * b;
            cross_y = cross_y + (override_screen_y - cross_y) * b;
            break;
        }
    }

    static constexpr float kArmLen = 6.0f;
    static constexpr float kThick = 2.0f;
    static constexpr float kGap = 2.0f;
    const Color color{1.0f, 1.0f, 1.0f, 0.8f};

    UIRenderer::drawRect(cross_x - kGap - kArmLen, cross_y - kThick * 0.5f, kArmLen, kThick, color);
    UIRenderer::drawRect(cross_x + kGap, cross_y - kThick * 0.5f, kArmLen, kThick, color);
    UIRenderer::drawRect(cross_x - kThick * 0.5f, cross_y - kGap - kArmLen, kThick, kArmLen, color);
    UIRenderer::drawRect(cross_x - kThick * 0.5f, cross_y + kGap, kThick, kArmLen, color);
}

} // namespace CrosshairRenderer
