#include "TextureManager.h"

#include <iostream>

#include <glad/glad.h>

// stb_image — single-header PNG/JPEG/etc. loader.
// STB_IMAGE_IMPLEMENTATION must be defined in exactly one .cpp file.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

uint32_t TextureManager::load(const std::string& path)
{
    if (path.empty())
        return makeFallback();

    // Cache hit — return immediately without touching disk or GPU.
    auto it = cache.find(path);
    if (it != cache.end())
        return it->second.id;

    // Load pixel data from disk.
    // stbi_load returns RGBA data (4 bytes per pixel), origin at top-left.
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        std::cerr << "[TextureManager] Failed to load: " << path << " (skipped)\n";
        cache[path] = {0, 0, 0};
        return 0;
    }

    // Upload to GPU.
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // sRGB-decode on sample: PNG art is authored in sRGB; the
    // shader pipeline expects linear-light inputs after the sRGB
    // framebuffer rewrite. GL_SRGB8_ALPHA8 internal format makes
    // the GPU do the decode in hardware at sample time.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);

    stbi_image_free(pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    TextureInfo info;
    info.id = static_cast<uint32_t>(texId);
    info.width = width;
    info.height = height;
    cache[path] = info;
    return info.id;
}

void TextureManager::getDimensions(const std::string& path, int& width, int& height) const
{
    auto it = cache.find(path);
    if (it != cache.end())
    {
        width = it->second.width;
        height = it->second.height;
    }
    else
    {
        width = 0;
        height = 0;
    }
}

void TextureManager::clear()
{
    for (auto& [path, info] : cache)
    {
        const GLuint texId = static_cast<GLuint>(info.id);
        glDeleteTextures(1, &texId);
    }
    cache.clear();

    if (fallback_id != 0)
    {
        const GLuint texId = static_cast<GLuint>(fallback_id);
        glDeleteTextures(1, &texId);
        fallback_id = 0;
    }
}

uint32_t TextureManager::makeFallback()
{
    if (fallback_id != 0)
        return fallback_id;

    // 8x8 magenta (255,0,255) / black checkerboard — unmistakably "missing texture".
    constexpr int size = 8;
    uint8_t pixels[size * size * 4];
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const bool checker = ((x + y) % 2 == 0);
            const int idx = (y * size + x) * 4;
            pixels[idx + 0] = checker ? 255 : 0; // R
            pixels[idx + 1] = 0;                 // G
            pixels[idx + 2] = checker ? 255 : 0; // B
            pixels[idx + 3] = 255;               // A
        }
    }

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // sRGB to match the real loader above; the magenta checkerboard
    // is meant to be visually obvious and the value stays pure-magenta
    // through the decode, which is the goal.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    fallback_id = static_cast<uint32_t>(texId);
    fallback_width = size;
    fallback_height = size;
    return fallback_id;
}
