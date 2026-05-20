#pragma once

#include "FontManager.h"

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// UIRenderer -- batched screen-space quad and text rendering.
//
// Static singleton. Uses its own GL shader with a screen-space orthographic
// projection ((0,0) = top-left, (windowW, windowH) = bottom-right).
// No camera offset -- UI elements stay fixed on screen.
//
// Usage:
//   UIRenderer::beginFrame();
//   UIRenderer::drawRect(10, 10, 200, 20, {1,0,0,1});
//   UIRenderer::drawText(font, "HP 50/100", 15, 12, {1,1,1,1});
//   UIRenderer::endFrame();
// ---------------------------------------------------------------------------

struct Color
{
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct TextSize
{
    float width = 0.0f;
    float height = 0.0f;
};

// Screen-space axis-aligned rectangle. Used for both destination
// rects (pixel coords) and UV rects (0..1 texture-atlas coords).
struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class UIRenderer
{
  public:
    static void init(int window_w, int window_h);
    static void resize(int window_w, int window_h);
    static void shutdown();

    // Call before any draw calls each frame.
    static void beginFrame();
    // Flush all batched draws to the screen.
    static void endFrame();

    // Draw all currently batched content without ending the frame.
    // Allows GL state changes (e.g. glScissor) to take effect mid-frame.
    static void flush();

    // Solid colored rectangle.
    static void drawRect(float x, float y, float w, float h, const Color& color);

    // Textured rectangle with optional tint. `uv` defaults to the
    // full texture (0,0,1,1).
    static void drawTexturedRect(const Rect& dst, uint32_t tex_id,
                                 const Rect& uv = {0.0f, 0.0f, 1.0f, 1.0f}, const Color& tint = {});

    // Render a text string. Returns the advance width.
    static float drawText(FontHandle font, const std::string& text, float x, float y,
                          const Color& color);

    // Measure text dimensions without drawing.
    static TextSize measureText(FontHandle font, const std::string& text);

    // Window dimensions for layout calculations.
    static int windowWidth();
    static int windowHeight();
};
