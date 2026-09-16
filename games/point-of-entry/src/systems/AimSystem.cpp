#include "systems/AimSystem.h"

#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/FeelConfig.h"
#include "renderers/DebugPanelRenderer.h"

#include <SDL.h>

#include <cmath>

#include <entt/entt.hpp>

namespace aim
{
namespace
{
float sDirX = 1.0f;
float sDirY = 0.0f;
float sWorldX = 0.0f;
float sWorldY = 0.0f;
bool sFiring = false;
bool sGuarding = false;
bool sPressed = false;
// True while a button that was already held at the start of play has not yet been let go --
// one per hand, because either button can be the one that closed a menu.
bool sStaleTrigger = false;
bool sStaleGuard = false;

// Below this many pixels from the player, the cursor gives no usable direction -- the vector is
// mostly rounding error and the aim would spin wildly. Hold the last direction instead.

} // namespace

void update(const Engine& engine, EntityManager& em)
{
    int mx = 0;
    int my = 0;
    const Uint32 buttons = SDL_GetMouseState(&mx, &my);
    const bool down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    // A trigger held over from a menu is not an order to fire. It stops being stale the moment it
    // is released, so the first REAL press works normally.
    if (sStaleTrigger && !down)
        sStaleTrigger = false;
    const bool live = down && !sStaleTrigger;
    sPressed = live && !sFiring;
    sFiring = live;
    // The other hand: RMB is the guard, held like the trigger is held -- and levelled the same
    // way, so the right-click that closed a menu never surfaces as a raised guard on resume.
    const bool guardHeld = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    if (sStaleGuard && !guardHeld)
        sStaleGuard = false;
    sGuarding = guardHeld && !sStaleGuard;

    // Screen to world. The window is an integer upscale of the internal buffer, so dividing by
    // the zoom converts a window pixel to a world pixel; the camera is the world position of the
    // buffer's top-left corner.
    const auto& reg = em.registry();
    const auto view = reg.view<const Camera, const Transform>();
    for (const auto entity : view)
    {
        const auto& cam = view.get<const Camera>(entity);
        const auto& t = view.get<const Transform>(entity);
        const float zoom = static_cast<float>(debug_panel::zoom());
        const float halfW = static_cast<float>(engine.windowWidth()) / (2.0f * zoom);
        const float halfH = static_cast<float>(engine.windowHeight()) / (2.0f * zoom);
        sWorldX = cam.x - halfW + static_cast<float>(mx) / zoom;
        sWorldY = cam.y - halfH + static_cast<float>(my) / zoom;

        const float dx = sWorldX - t.x;
        const float dy = sWorldY - t.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > feel::current().aim.dead_zone)
        {
            sDirX = dx / len;
            sDirY = dy / len;
        }
        break; // one camera, and it is the player's
    }
}

float dirX()
{
    return sDirX;
}
float dirY()
{
    return sDirY;
}
float worldX()
{
    return sWorldX;
}
float worldY()
{
    return sWorldY;
}
void requireFreshPress()
{
    sStaleTrigger = true;
    sStaleGuard = true;
    sFiring = false;
    sPressed = false;
    // Down as well as stale: consumers run after this in the same tick, and none may see the
    // guard up -- or watch it drop -- for a press that belonged to a menu.
    sGuarding = false;
}

bool firing()
{
    return sFiring;
}

bool guarding()
{
    return sGuarding;
}
bool firePressed()
{
    return sPressed;
}

} // namespace aim
