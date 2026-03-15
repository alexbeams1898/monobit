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
// Missing files: load() logs a warning and returns a magenta checkerboard
// fallback texture so the engine never crashes on a missing asset.
//
// Lifetime: call clear() in Engine::shutdown() to destroy all GL textures
// before the GL context is destroyed.
// ---------------------------------------------------------------------------

class TextureManager
{
  public:
    // Load a PNG from disk (or return cached handle). Requires a live GL context.
    uint32_t load(const std::string& path);

    // Destroy all GL textures. Call before destroying the GL context.
    void clear();

  private:
    // Generate an 8x8 magenta checkerboard texture used when an asset is missing.
    uint32_t makeFallback();

    std::unordered_map<std::string, uint32_t> cache_;
    uint32_t fallbackId_ = 0;
};
