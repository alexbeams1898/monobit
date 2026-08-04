#include "ShellInput.h"

#include <SDL.h>

namespace shell_input
{
namespace
{
// Held-last-frame state, so every key here reports an EDGE rather than a level.
bool sWas[SDL_NUM_SCANCODES] = {};
bool sWasLeft = false;

bool pressed(const Uint8* keys, SDL_Scancode key)
{
    const bool now = keys[key] != 0;
    const bool edge = now && !sWas[key];
    sWas[key] = now;
    return edge;
}
} // namespace

Frame read()
{
    Frame f;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    // Both clusters, because a menu should answer to whichever hand is on the keyboard.
    f.up = pressed(keys, SDL_SCANCODE_W) | pressed(keys, SDL_SCANCODE_UP);
    f.down = pressed(keys, SDL_SCANCODE_S) | pressed(keys, SDL_SCANCODE_DOWN);
    f.confirm = pressed(keys, SDL_SCANCODE_SPACE) | pressed(keys, SDL_SCANCODE_RETURN);
    f.back = pressed(keys, SDL_SCANCODE_ESCAPE);

    int mx = 0;
    int my = 0;
    const Uint32 buttons = SDL_GetMouseState(&mx, &my);
    f.mouse_x = static_cast<float>(mx);
    f.mouse_y = static_cast<float>(my);
    const bool left = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    f.clicked = left && !sWasLeft;
    sWasLeft = left;
    return f;
}

} // namespace shell_input
