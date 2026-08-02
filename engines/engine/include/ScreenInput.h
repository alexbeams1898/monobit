#pragma once

#include "ecs/EntityManager.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>

// ---------------------------------------------------------------------------
// ScreenInput -- generic hit-testing + click helpers for screen-space UI
// (pause pages, menus, dialogs). Pairs with UIRenderer: a screen draws rects
// with UIRenderer, then hit-tests the mouse against those same rects here.
//
// Engine-level so every game's screens share one implementation of the
// click-consume flag and the hover math (rather than each game reinventing it).
// Game-specific input (a UI click sound, gameplay key bindings) stays in the
// game; only the generic geometry/consume plumbing lives here.
// ---------------------------------------------------------------------------

namespace engine::ui
{

// A left/right mouse button was pressed this frame. Marks the corresponding
// consume flag so gameplay systems (and other click consumers) can tell the
// click was taken by UI. Does not erase from the event buffer -- multiple UI
// elements in the same frame may hit-test the same click; the buffer clears at
// end of frame.
inline bool mouseClicked(EntityManager& em, uint8_t button)
{
    const auto& events = em.mouse_down_events;
    if (std::find(events.begin(), events.end(), button) == events.end())
        return false;
    if (button == SDL_BUTTON_LEFT)
        em.lmb_consumed = true;
    else if (button == SDL_BUTTON_RIGHT)
        em.rmb_consumed = true;
    return true;
}

// True if (mx,my) lies within the rectangle [x, x+w) x [y, y+h). The building
// block for hover/click on any drawn element.
inline bool pointInRect(float mx, float my, float x, float y, float w, float h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

} // namespace engine::ui
