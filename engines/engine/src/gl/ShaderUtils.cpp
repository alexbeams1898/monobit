#include "gl/ShaderUtils.h"

#include <cmath>
#include <iostream>

namespace engine::gl
{

GLuint compileShader(GLenum type, const char* src)
{
    const GLuint shader = glCreateShader(type);
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

void buildModelRotated(float mat[16], float x, float y, float w, float h, float rot, bool mirror)
{
    // Unit quad [0,1]^2 -> mirror(optional) -> scale (w,h) -> rotate -> translate.
    // The center of rotation is always at (x + w/2, y + h/2) regardless of
    // mirror. mirror negates the x-scale so the image flips horizontally
    // around that center, composing correctly with the rotation.
    const float mw = mirror ? -w : w;
    const float c = std::cos(rot);
    const float s = std::sin(rot);
    const float hw = w * 0.5f; // always positive — used for center computation
    const float hh = h * 0.5f;
    const float mhw = mw * 0.5f; // may be negative — used for scale in the matrix
    // clang-format off
    mat[ 0] = c * mw;  mat[ 4] = -s * h;  mat[ 8] = 0.0f;  mat[12] = x + hw - c * mhw + s * hh;
    mat[ 1] = s * mw;  mat[ 5] =  c * h;  mat[ 9] = 0.0f;  mat[13] = y + hh - s * mhw - c * hh;
    mat[ 2] = 0.0f;    mat[ 6] = 0.0f;    mat[10] = 1.0f;   mat[14] = 0.0f;
    mat[ 3] = 0.0f;    mat[ 7] = 0.0f;    mat[11] = 0.0f;   mat[15] = 1.0f;
    // clang-format on
}

} // namespace engine::gl
