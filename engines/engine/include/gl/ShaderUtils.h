#pragma once

#include <glad/glad.h>

namespace engine::gl
{

// Compile a single shader (vertex or fragment). Logs errors to stderr.
GLuint compileShader(GLenum type, const char* src);

// Compile vertex + fragment shaders and link them into a program. Returns the
// program ID, or 0 if any stage failed (compile or link). Logs errors to stderr.
GLuint compileProgram(const char* vert_src, const char* frag_src);

// Build a 4x4 orthographic projection matrix (column-major, OpenGL convention).
void buildOrtho(float mat[16], float left, float right, float bottom, float top);

// Build a 4x4 model matrix (translate + scale, column-major).
void buildModel(float mat[16], float x, float y, float w, float h);

// Build a 4x4 model matrix with rotation around the sprite center (radians,
// clockwise with positive Y axis going down on screen). When mirror is true,
// the sprite is horizontally mirrored at the geometry level (composes
// correctly with rotation, unlike UV-based flip_x).
void buildModelRotated(float mat[16], float x, float y, float w, float h, float rot,
                       bool mirror = false);

} // namespace engine::gl
