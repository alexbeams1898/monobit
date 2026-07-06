#include "render/Texture.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

namespace
{
// Shared upload path: takes an RGBA pixel buffer (already inverted /
// processed as needed), creates the GL texture, sets standard sampler
// params + anisotropy.
std::uint32_t uploadRGBA(const unsigned char* data, int w, int h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // sRGB-decode on sample: PNG art is authored sRGB; shaders want
    // linear-light inputs. GL_SRGB8_ALPHA8 does the hardware decode
    // at sample time AND makes glGenerateMipmap downsample in
    // linear space (mathematically correct, vs the buggy gray
    // mip-fade you get when sRGB-encoded bytes get averaged raw).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
    constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
    GLfloat max_aniso = 1.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &max_aniso);
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyExt, std::min(16.0f, max_aniso));
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
} // namespace

std::uint32_t loadTexture2DWithSize(const std::string& path, int& out_w, int& out_h)
{
    out_w = 0;
    out_h = 0;
    int w = 0;
    int h = 0;
    int channels = 0;
    // glTF stores textures with origin top-left and UVs that match;
    // OpenGL's default origin is bottom-left. We previously flipped
    // on load, but that meant leaf-card geometry sampled wood-region
    // alpha (and vice versa), making "stray sticks" appear in the
    // branches mesh where leaves should be. Don't flip — instead let
    // the shader / UVs handle it.
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (data == nullptr)
    {
        std::fprintf(stderr, "[texture] failed to load %s: %s\n", path.c_str(),
                     stbi_failure_reason());
        return 0;
    }
    const std::uint32_t tex = uploadRGBA(data, w, h);
    stbi_image_free(data);
    out_w = w;
    out_h = h;
    return tex;
}

std::uint32_t loadTexture2D(const std::string& path)
{
    int w = 0;
    int h = 0;
    return loadTexture2DWithSize(path, w, h);
}

void destroyTexture(std::uint32_t tex)
{
    if (tex != 0)
    {
        glDeleteTextures(1, &tex);
    }
}

} // namespace selva::render
