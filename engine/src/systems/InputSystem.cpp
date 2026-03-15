#include "systems/InputSystem.h"

#include "ecs/Components.h"

#include <SDL.h>
#include <cmath>

void InputSystem::update(EntityManager& em)
{
    // SDL_GetKeyboardState returns a pointer into SDL's internal key table.
    // It is updated by SDL_PollEvent — call this after the event loop, not before.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    float mx = 0.0f;
    float my = 0.0f;

    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        mx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        mx += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        my -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        my += 1.0f;

    // Normalise diagonal movement so holding W+D is the same speed as holding W alone.
    const float len = std::sqrt(mx * mx + my * my);
    if (len > 0.0f)
    {
        mx /= len;
        my /= len;
    }

    for (auto [entity, input] : em.registry().view<Input>().each())
    {
        input.moveX = mx;
        input.moveY = my;
    }
}
