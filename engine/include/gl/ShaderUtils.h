#pragma once

#include <glad/glad.h>

namespace engine::gl
{

// Compile a single shader (vertex or fragment). Logs errors to stderr.
GLuint compileShader(GLenum type, const char* src);

// Build a 4x4 orthographic projection matrix (column-major, OpenGL convention).
void buildOrtho(float mat[16], float left, float right, float bottom, float top);

// Build a 4x4 model matrix (translate + scale, column-major).
void buildModel(float mat[16], float x, float y, float w, float h);

} // namespace engine::gl
