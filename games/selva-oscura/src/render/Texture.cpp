#include "render/Texture.h"

#include <stb_image.h>

#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

std::uint32_t loadTexture2D(const std::string& path)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
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
