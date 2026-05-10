#pragma once

#include "ecs/EntityManager.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>

namespace screen_input
{

inline bool keyPressed(const EntityManager& em, int scancode)
{
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

inline bool mouseClicked(EntityManager& em, uint8_t button)
{
    const auto& events = em.mouse_down_events;
    if (std::find(events.begin(), events.end(), button) == events.end())
        return false;
    // Mark consumed so game systems (combat, etc.) don't act on the same
    // click. Don't erase from the buffer — other UI elements in the same
    // render frame may need to see it. Buffer clears at end of frame.
    if (button == SDL_BUTTON_LEFT)
        em.lmb_consumed = true;
    else if (button == SDL_BUTTON_RIGHT)
        em.rmb_consumed = true;
    return true;
}

inline int hoveredRow(float mx, float my, float cx, float cy, float cw, float row_h, int count)
{
    if (mx < cx - 4.0f || mx >= cx + cw + 4.0f || my < cy - 2.0f)
        return -1;
    const int idx = static_cast<int>((my - (cy - 2.0f)) / row_h);
    return (idx >= 0 && idx < count) ? idx : -1;
}

inline void playClickSfx(const EntityManager& em)
{
    const auto& click = em.registry().ctx().get<SoundConfig>().get("ui_click");
    if (!click.path.empty())
        AudioSystem::playSfx(click.path, click.volume);
}

} // namespace screen_input
