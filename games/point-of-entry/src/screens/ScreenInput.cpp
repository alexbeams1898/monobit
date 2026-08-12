#include "screens/ScreenInput.h"

#include <SDL.h>

namespace shell_input
{
namespace
{
// Held-last-frame state, so every key here reports an EDGE rather than a level.
bool sWas[SDL_NUM_SCANCODES] = {};
bool sWasLeft = false;
bool sWasRight = false;
// True for exactly one frame after a right-click back fired, so the press that closed a
// screen can never read as a second step across the swap it caused.
bool sSwallowRight = false;
// Where the pointer was, so a screen can tell a moving cursor from a resting one.
int sLastX = -1;
int sLastY = -1;

bool pressed(const Uint8* keys, SDL_Scancode key)
{
    const bool now = keys[key] != 0;
    const bool edge = now && !sWas[key];
    sWas[key] = now;
    return edge;
}
} // namespace

Frame read(int wheel)
{
    Frame f;
    f.mouse.wheel = wheel;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    // Both clusters, because a menu should answer to whichever hand is on the keyboard.
    f.up = pressed(keys, SDL_SCANCODE_W) | pressed(keys, SDL_SCANCODE_UP);
    f.down = pressed(keys, SDL_SCANCODE_S) | pressed(keys, SDL_SCANCODE_DOWN);
    f.left = pressed(keys, SDL_SCANCODE_A) | pressed(keys, SDL_SCANCODE_LEFT);
    f.right = pressed(keys, SDL_SCANCODE_D) | pressed(keys, SDL_SCANCODE_RIGHT);
    f.confirm = pressed(keys, SDL_SCANCODE_SPACE) | pressed(keys, SDL_SCANCODE_RETURN);
    const bool esc = pressed(keys, SDL_SCANCODE_ESCAPE);
    f.menu = esc;

    int mx = 0;
    int my = 0;
    const Uint32 buttons = SDL_GetMouseState(&mx, &my);
    f.mouse.x = static_cast<float>(mx);
    f.mouse.y = static_cast<float>(my);
    f.mouse.moved = mx != sLastX || my != sLastY;
    sLastX = mx;
    sLastY = my;
    const bool left = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    f.mouse.clicked = left && !sWasLeft;
    sWasLeft = left;
    // Right-click is the family's back, the same step Esc takes -- one definition here, so no
    // screen grows its own reading of the button the gameplay hand also owns.
    const bool right = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    const bool rightBack = right && !sWasRight && !sSwallowRight;
    sSwallowRight = rightBack;
    sWasRight = right;
    f.back = esc || rightBack;
    return f;
}

int step(int cursor, int count, bool up, bool down)
{
    if (count <= 0)
        return kNoChoice;
    if (up)
        return cursor == kNoChoice ? count - 1 : (cursor - 1 + count) % count;
    if (down)
        return cursor == kNoChoice ? 0 : (cursor + 1) % count;
    return cursor;
}

int hover(int cursor, int hovered, bool moved)
{
    if (hovered != kNoChoice)
        return hovered;
    return moved ? kNoChoice : cursor;
}

int scrollTop(int top, int rows, int visible, int wheel, int follow)
{
    const int last = rows > visible ? rows - visible : 0;
    int t = top - wheel;
    if (follow != kNoChoice)
    {
        if (follow < t)
            t = follow;
        if (follow >= t + visible)
            t = follow - visible + 1;
    }
    if (t > last)
        t = last;
    return t < 0 ? 0 : t;
}

} // namespace shell_input
