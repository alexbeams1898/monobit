#include "SpriteCompositor.h"

#include <glad/glad.h>
#include <iostream>
#include <stb_image.h>

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

uint32_t SpriteCompositor::composite(const std::vector<std::string>& layer_paths)
{
    const std::string key = buildCacheKey(layer_paths);
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second.tex_id;

    int final_w = 0;
    int final_h = 0;
    std::vector<uint8_t> buffer;

    for (const auto& path : layer_paths)
    {
        if (path.empty())
            continue;

        int w = 0;
        int h = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            std::cerr << "[SpriteCompositor] Failed to load layer: " << path << "\n";
            continue;
        }

        if (final_w == 0)
        {
            // First valid layer sets dimensions and becomes the base.
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

        // Alpha-composite this layer on top of the buffer ("over" operator).
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

            // General case: dst = src + dst * (1 - src_alpha/255).
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

    // Upload composited pixel buffer to GL.
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
