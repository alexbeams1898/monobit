#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// SpriteCompositor -- composites multiple sprite sheet layer PNGs into a
// single GL texture via CPU-side alpha blending.
//
// Each layer PNG must have identical dimensions (same frame layout). Layers
// are composited bottom-to-top (index 0 = back, last = front).
//
// Results are cached by a key derived from the layer path list. Identical
// layer combinations share one GL texture.
//
// Thread safety: not thread-safe. Call only from the main (GL) thread.
// ---------------------------------------------------------------------------

struct CompositeTextureInfo
{
    uint32_t tex_id = 0;
    int width = 0;
    int height = 0;
};

class SpriteCompositor
{
  public:
    // Composite multiple layer PNGs (bottom-to-top order) into one GL texture.
    // Empty strings in layer_paths are skipped (represent "none" slots).
    // Returns the GL texture ID, or 0 on failure (no valid layers).
    uint32_t composite(const std::vector<std::string>& layer_paths);

    // Retrieve dimensions of a previously composited texture.
    // Returns false if tex_id is not in the cache.
    bool getDimensions(uint32_t tex_id, int& width, int& height) const;

    // Destroy all cached GL textures. Call before GL context destruction.
    void clear();

    // Build a cache key from layer paths (joined by '|').
    static std::string buildCacheKey(const std::vector<std::string>& layer_paths);

  private:
    std::unordered_map<std::string, CompositeTextureInfo> cache;
    std::unordered_map<uint32_t, CompositeTextureInfo*> id_lookup;
};
