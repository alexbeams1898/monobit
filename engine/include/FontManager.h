#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// FontManager -- loads .ttf fonts via stb_truetype and bakes glyph atlases.
//
// Static singleton like AudioSystem / RenderSystem. Call init() once after
// gladLoadGL, shutdown() before destroying the GL context.
//
// Usage:
//   FontManager::init();
//   int font = FontManager::loadFont("assets/fonts/gothic.ttf", 24.0f);
//   FontManager::shutdown();
// ---------------------------------------------------------------------------

struct GlyphInfo
{
    float u0, v0, u1, v1; // UV coordinates in atlas
    float x_off, y_off;   // offset from cursor to top-left of glyph
    float width, height;  // glyph bitmap dimensions (pixels)
    float advance;        // horizontal advance after this glyph
};

using FontHandle = int;
static constexpr FontHandle INVALID_FONT = -1;

class FontManager
{
  public:
    static void init();
    static void shutdown();

    // Load a font at a specific pixel size. Returns a handle for drawText().
    // Multiple calls with different sizes create separate atlas textures.
    static FontHandle loadFont(const std::string& path, float size_px);

    // Look up glyph metrics for a character (ASCII 32-126).
    static const GlyphInfo* glyph(FontHandle handle, char ch);

    // Get the GL texture ID for a font's glyph atlas.
    static uint32_t atlasTexture(FontHandle handle);

    // Get the line height (ascent - descent + gap) for layout.
    static float lineHeight(FontHandle handle);

    // Get the font ascent (distance from baseline to top of tallest glyph).
    static float ascent(FontHandle handle);
};
