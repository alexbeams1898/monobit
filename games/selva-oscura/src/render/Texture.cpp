#include "render/Texture.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

std::uint32_t loadTexture2D(const std::string& path)
{
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
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Anisotropic filtering reduces aliasing on textures sampled at
    // shallow angles. Use the EXT_texture_filter_anisotropic extension
    // constants - the core promotion landed in OpenGL 4.6, but our
    // glad is set up for 3.3. The constants are widely supported
    // since 2003 hardware so this is safe to enable unconditionally.
    constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
    constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
    GLfloat max_aniso = 1.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &max_aniso);
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyExt, std::min(16.0f, max_aniso));
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

void destroyTexture(std::uint32_t tex)
{
    if (tex != 0)
    {
        glDeleteTextures(1, &tex);
    }
}

} // namespace selva::render
