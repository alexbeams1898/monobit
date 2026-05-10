#include "SpriteCompositor.h"

#include <stb_image.h>

#include <iostream>

#include <glad/glad.h>

namespace
{

// Default LPC humanoid sheet dimensions, used when a missing layer is the very first
// (no other layer has set dimensions yet). Matches assemble_spritesheet.py output.
constexpr int FALLBACK_W = 3328;
constexpr int FALLBACK_H = 640;
constexpr int CHECKER_TILE = 16;

// Build a magenta-and-black checkerboard so missing layers are screamingly visible.
// Magenta = (255, 0, 255), Black = (0, 0, 0). Fully opaque so it covers other layers.
std::vector<uint8_t> makeCheckerboard(int w, int h)
{
    std::vector<uint8_t> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const bool on = ((x / CHECKER_TILE) + (y / CHECKER_TILE)) % 2 == 0;
            const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + x) * 4;
            buf[off + 0] = on ? 255 : 0;
            buf[off + 1] = 0;
            buf[off + 2] = on ? 255 : 0;
            buf[off + 3] = 255;
        }
    }
    return buf;
}

} // namespace

std::string SpriteCompositor::buildCacheKey(const std::vector<std::string>& layer_paths)
{
    std::string key;
    for (const auto& path : layer_paths)
    {
        if (!key.empty())
            key += '|';
        key += path;
    }
    return key;
}

// Apply a palette swap to a loaded pixel buffer in-place.
static void applyPaletteSwap(stbi_uc* pixels, int w, int h, const PaletteSwap& palette)
{
    const size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < pixel_count; ++i)
    {
        const size_t off = i * 4;
        if (pixels[off + 3] == 0)
            continue;
        const uint8_t r = pixels[off + 0];
        const uint8_t g = pixels[off + 1];
        const uint8_t b = pixels[off + 2];
        for (const auto& e : palette.entries)
        {
            if (r == e.base_r && g == e.base_g && b == e.base_b)
            {
                pixels[off + 0] = e.target_r;
                pixels[off + 1] = e.target_g;
                pixels[off + 2] = e.target_b;
                break;
            }
        }
    }
}

uint32_t SpriteCompositor::composite(const std::vector<std::string>& layer_paths)
{
    return composite(layer_paths, {});
}

// Append palette identifiers onto an existing layer-paths cache key
// so different recolorings of the same master sprite produce
// distinct cache entries.
static void appendPaletteToCacheKey(std::string& key, const std::vector<PaletteSwap>& palettes)
{
    for (size_t i = 0; i < palettes.size(); ++i)
    {
        if (palettes[i].empty())
            continue;
        key += "|pal" + std::to_string(i) + ":";
        for (const auto& e : palettes[i].entries)
        {
            key += std::to_string(e.target_r) + "," + std::to_string(e.target_g) + "," +
                   std::to_string(e.target_b) + ";";
        }
    }
}

// Per-pixel "source over" alpha-blend src into dst (both 4-byte
// RGBA). Skips fully-transparent source pixels and fast-paths fully
// opaque ones.
static void alphaBlendOver(const uint8_t* src, uint8_t* dst, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; ++i)
    {
        const size_t offset = i * 4;
        const uint8_t* s = src + offset;
        uint8_t* d = dst + offset;
        const uint32_t sa = s[3];
        if (sa == 0)
            continue;
        if (sa == 255)
        {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
            continue;
        }
        const uint32_t inv_sa = 255 - sa;
        d[0] = static_cast<uint8_t>((s[0] * sa + d[0] * inv_sa) / 255);
        d[1] = static_cast<uint8_t>((s[1] * sa + d[1] * inv_sa) / 255);
        d[2] = static_cast<uint8_t>((s[2] * sa + d[2] * inv_sa) / 255);
        d[3] = static_cast<uint8_t>(sa + (d[3] * inv_sa) / 255);
    }
}

// Composite the named layer onto `buffer`. The first non-empty layer
// seeds the buffer dimensions; subsequent layers must match. Empty
// paths and missing files are skipped with a stderr warning. Layer-
// specific palette swap is applied before blending.
static void compositeOneLayer(const std::string& path, const PaletteSwap* palette, int& final_w,
                              int& final_h, std::vector<uint8_t>& buffer)
{
    if (path.empty())
        return;
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        std::cerr << "[SpriteCompositor] MISSING LAYER (skipped): " << path << "\n";
        return;
    }
    if (palette != nullptr && !palette->empty())
        applyPaletteSwap(pixels, w, h, *palette);

    if (final_w == 0)
    {
        final_w = w;
        final_h = h;
        const size_t byte_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        buffer.assign(pixels, pixels + byte_count);
    }
    else if (w != final_w || h != final_h)
    {
        std::cerr << "[SpriteCompositor] Layer size mismatch (" << w << "x" << h << " vs "
                  << final_w << "x" << final_h << "): " << path << "\n";
    }
    else
    {
        const size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
        alphaBlendOver(pixels, buffer.data(), pixel_count);
    }
    stbi_image_free(pixels);
}

// Upload a freshly-composited RGBA buffer as a new GL texture with
// nearest-neighbor + clamp-to-edge filtering (pixel-art appropriate).
static GLuint uploadCompositeTexture(const std::vector<uint8_t>& buffer, int w, int h)
{
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texId;
}

uint32_t SpriteCompositor::composite(const std::vector<std::string>& layer_paths,
                                     const std::vector<PaletteSwap>& palettes)
{
    std::string key = buildCacheKey(layer_paths);
    appendPaletteToCacheKey(key, palettes);
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second.tex_id;

    int final_w = 0;
    int final_h = 0;
    std::vector<uint8_t> buffer;
    for (size_t layerIdx = 0; layerIdx < layer_paths.size(); ++layerIdx)
    {
        const PaletteSwap* palette = (layerIdx < palettes.size()) ? &palettes[layerIdx] : nullptr;
        compositeOneLayer(layer_paths[layerIdx], palette, final_w, final_h, buffer);
    }
    if (buffer.empty())
        return 0;

    const auto tid = static_cast<uint32_t>(uploadCompositeTexture(buffer, final_w, final_h));
    auto& info = cache[key];
    info.tex_id = tid;
    info.width = final_w;
    info.height = final_h;
    id_lookup[tid] = &info;
    return tid;
}

bool SpriteCompositor::getDimensions(uint32_t tex_id, int& width, int& height) const
{
    auto it = id_lookup.find(tex_id);
    if (it == id_lookup.end())
    {
        width = 0;
        height = 0;
        return false;
    }
    width = it->second->width;
    height = it->second->height;
    return true;
}

void SpriteCompositor::clear()
{
    for (auto& [key, info] : cache)
    {
        const GLuint texId = static_cast<GLuint>(info.tex_id);
        glDeleteTextures(1, &texId);
    }
    cache.clear();
    id_lookup.clear();
}
