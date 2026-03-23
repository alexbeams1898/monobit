#include "gl/ShaderUtils.h"

#include <iostream>

namespace engine::gl
{

GLuint compileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[GL] Shader compile error:\n" << log << "\n";
    }
    return shader;
}

void buildOrtho(float mat[16], float left, float right, float bottom, float top)
{
    const float rml = right - left;
    const float tmb = top - bottom;

    // clang-format off
    mat[ 0] = 2.0f / rml;  mat[ 4] = 0.0f;         mat[ 8] = 0.0f;  mat[12] = -(right + left) / rml;
    mat[ 1] = 0.0f;        mat[ 5] = 2.0f / tmb;   mat[ 9] = 0.0f;  mat[13] = -(top + bottom) / tmb;
    mat[ 2] = 0.0f;        mat[ 6] = 0.0f;          mat[10] = -1.0f; mat[14] = 0.0f;
    mat[ 3] = 0.0f;        mat[ 7] = 0.0f;          mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

void buildModel(float mat[16], float x, float y, float w, float h)
{
    // clang-format off
    mat[ 0] = w;     mat[ 4] = 0.0f;  mat[ 8] = 0.0f;  mat[12] = x;
    mat[ 1] = 0.0f;  mat[ 5] = h;     mat[ 9] = 0.0f;  mat[13] = y;
    mat[ 2] = 0.0f;  mat[ 6] = 0.0f;  mat[10] = 1.0f;  mat[14] = 0.0f;
    mat[ 3] = 0.0f;  mat[ 7] = 0.0f;  mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

} // namespace engine::gl
