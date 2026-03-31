#pragma once

#include "UIRenderer.h"

#include <cmath>

// ---------------------------------------------------------------------------
// DebugDraw -- world-space debug drawing primitives.
//
// Wraps UIRenderer with automatic world-to-screen coordinate conversion.
// Call setCamera() once per frame before any draw calls. All positions are
// in world coordinates; the camera transform is applied internally.
//
// Zero cost when not called. No allocations. All draws go through
// UIRenderer's existing batch so they share the same draw call.
//
// Usage:
//   DebugDraw::setCamera(camX, camY, windowW, windowH);
//   DebugDraw::dot(enemy.x, enemy.y, 4.0f, {1,0,0,1});
//   DebugDraw::circle(px, py, 48.0f, {1,1,1,0.3f});
// ---------------------------------------------------------------------------

namespace DebugDraw
{

// Internal state -- declared before functions that use them.
inline float sCamX = 0.0f;
inline float sCamY = 0.0f;
inline float sHalfW = 0.0f;
inline float sHalfH = 0.0f;

// Must be called once per frame before any draw calls.
inline void setCamera(float cam_x, float cam_y, int window_w, int window_h)
{
    sCamX = cam_x;
    sCamY = cam_y;
    sHalfW = static_cast<float>(window_w) * 0.5f;
    sHalfH = static_cast<float>(window_h) * 0.5f;
}

// Solid square centered at a world position.
inline void dot(float world_x, float world_y, float size, const Color& color)
{
    const float sx = world_x - sCamX + sHalfW;
    const float sy = world_y - sCamY + sHalfH;
    UIRenderer::drawRect(sx - size * 0.5f, sy - size * 0.5f, size, size, color);
}

// Solid rectangle in world coordinates (top-left origin).
inline void rect(float world_x, float world_y, float w, float h, const Color& color)
{
    const float sx = world_x - sCamX + sHalfW;
    const float sy = world_y - sCamY + sHalfH;
    UIRenderer::drawRect(sx, sy, w, h, color);
}

// Dotted line between two world positions.
inline void line(float x1, float y1, float x2, float y2, const Color& color, float spacing = 6.0f,
                 float dot_size = 2.0f)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return;
    const int steps = static_cast<int>(len / spacing);
    if (steps <= 0)
        return;
    const float stepX = dx / static_cast<float>(steps);
    const float stepY = dy / static_cast<float>(steps);
    for (int i = 0; i <= steps; ++i)
    {
        const float wx = x1 + stepX * static_cast<float>(i);
        const float wy = y1 + stepY * static_cast<float>(i);
        dot(wx, wy, dot_size, color);
    }
}

// Circle approximated by evenly spaced dots.
inline void circle(float world_cx, float world_cy, float radius, const Color& color,
                   int segments = 24, float dot_size = 2.0f)
{
    const float step = 6.28318530f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i)
    {
        const float angle = step * static_cast<float>(i);
        const float wx = world_cx + std::cos(angle) * radius;
        const float wy = world_cy + std::sin(angle) * radius;
        dot(wx, wy, dot_size, color);
    }
}

// Accessors for current camera state (useful for view culling).
inline float camX()
{
    return sCamX;
}
inline float camY()
{
    return sCamY;
}
inline float halfW()
{
    return sHalfW;
}
inline float halfH()
{
    return sHalfH;
}

} // namespace DebugDraw
