#pragma once

#include "ecs/EntityManager.h"

#include <algorithm>
#include <cstdint>

namespace screen_input
{

inline bool keyPressed(const EntityManager& em, int scancode)
{
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

inline bool mouseClicked(const EntityManager& em, uint8_t button)
{
    for (uint8_t btn : em.mouse_down_events)
        if (btn == button)
            return true;
    return false;
}

inline int hoveredRow(float mx, float my, float cx, float cy, float cw, float row_h, int count)
{
    if (mx < cx - 4.0f || mx >= cx + cw + 4.0f || my < cy - 2.0f)
        return -1;
    int idx = static_cast<int>((my - (cy - 2.0f)) / row_h);
    return (idx >= 0 && idx < count) ? idx : -1;
}

} // namespace screen_input
