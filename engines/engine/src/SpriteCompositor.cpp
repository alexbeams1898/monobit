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

uint32_t SpriteCompositor::composite(const std::vector<std::string>& layer_paths,
                                     const std::vector<PaletteSwap>& palettes)
{
    // Build cache key including palette info so different colors with
    // the same master path produce different cache entries.
    std::string key = buildCacheKey(layer_paths);
    for (size_t i = 0; i < palettes.size(); ++i)
    {
        if (!palettes[i].empty())
        {
            key += "|pal" + std::to_string(i) + ":";
            for (const auto& e : palettes[i].entries)
            {
                key += std::to_string(e.target_r) + "," + std::to_string(e.target_g) + "," +
                       std::to_string(e.target_b) + ";";
            }
        }
    }
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second.tex_id;

    int final_w = 0;
    int final_h = 0;
    std::vector<uint8_t> buffer;

    for (size_t layerIdx = 0; layerIdx < layer_paths.size(); ++layerIdx)
    {
        const auto& path = layer_paths[layerIdx];
        if (path.empty())
            continue;

        int w = 0;
        int h = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            std::cerr << "[SpriteCompositor] MISSING LAYER (skipped): " << path << "\n";
            continue;
        }

        // Apply palette swap if provided for this layer.
        if (layerIdx < palettes.size() && !palettes[layerIdx].empty())
            applyPaletteSwap(pixels, w, h, palettes[layerIdx]);

        if (final_w == 0)
        {
            final_w = w;
            final_h = h;
            const size_t byte_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            buffer.assign(pixels, pixels + byte_count);
            stbi_image_free(pixels);
            continue;
        }

        if (w != final_w || h != final_h)
        {
            std::cerr << "[SpriteCompositor] Layer size mismatch (" << w << "x" << h << " vs "
                      << final_w << "x" << final_h << "): " << path << "\n";
            stbi_image_free(pixels);
            continue;
        }

        const size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < pixel_count; ++i)
        {
            const size_t offset = i * 4;
            const uint8_t* src = pixels + offset;
            uint8_t* dst = buffer.data() + offset;

            const uint32_t sa = src[3];
            if (sa == 0)
                continue;
            if (sa == 255)
            {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
                continue;
            }

            const uint32_t inv_sa = 255 - sa;
            dst[0] = static_cast<uint8_t>((src[0] * sa + dst[0] * inv_sa) / 255);
            dst[1] = static_cast<uint8_t>((src[1] * sa + dst[1] * inv_sa) / 255);
            dst[2] = static_cast<uint8_t>((src[2] * sa + dst[2] * inv_sa) / 255);
            dst[3] = static_cast<uint8_t>(sa + (dst[3] * inv_sa) / 255);
        }
        stbi_image_free(pixels);
    }

    if (buffer.empty())
        return 0;

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, final_w, final_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 buffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const auto tid = static_cast<uint32_t>(texId);
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
