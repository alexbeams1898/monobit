#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// TextureManager — loads PNG files into OpenGL textures and caches them.
//
// Usage:
//   uint32_t id = textureManager.load("assets/sprites.png");
//   glBindTexture(GL_TEXTURE_2D, id);
//
// Caching: calling load() with the same path returns the same GL handle
// immediately — no redundant disk reads or GPU uploads.
//
// Dimensions are cached at load time so callers (RenderSystem) can query
// texture size without a GPU readback (glGetTexLevelParameteriv).
//
// Missing files: load() logs a warning and returns a magenta checkerboard
// fallback texture so the engine never crashes on a missing asset.
//
// Lifetime: call clear() in Engine::shutdown() to destroy all GL textures
// before the GL context is destroyed.
// ---------------------------------------------------------------------------

struct TextureInfo
{
    uint32_t id = 0;
    int width = 0;
    int height = 0;
};

class TextureManager
{
  public:
    // Load a PNG from disk (or return cached handle). Requires a live GL context.
    uint32_t load(const std::string& path);

    // Return cached dimensions for a previously loaded texture path.
    // Returns (0, 0) if the path was never loaded.
    void getDimensions(const std::string& path, int& width, int& height) const;

    // Destroy all GL textures. Call before destroying the GL context.
    void clear();

  private:
    // Generate an 8x8 magenta checkerboard texture used when an asset is missing.
    uint32_t makeFallback();

    std::unordered_map<std::string, TextureInfo> cache;
    uint32_t fallback_id = 0;
    int fallback_width = 0;
    int fallback_height = 0;
};
