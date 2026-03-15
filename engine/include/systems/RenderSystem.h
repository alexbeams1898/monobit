#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// RenderSystem — STUB, replaced in Issue #6.
//
// Uses legacy OpenGL compatibility-profile functions (glBegin/glEnd, glOrtho)
// to draw entities as colored rectangles so there is something visible before
// real sprite rendering is built.
//
// Requires the GL context to use SDL_GL_CONTEXT_PROFILE_COMPATIBILITY.
// Issue #6 will switch back to core profile, add GLAD, and replace this with
// VAO/VBO/shader-based rendering.
//
// Any entity with Transform + Collider is drawn as a teal quad.
// The Collider dimensions supply the visual size (28x28 for the player).
// ---------------------------------------------------------------------------

class RenderSystem
{
  public:
    // Set up the orthographic projection. Call once after the GL context is
    // created. (0,0) maps to the top-left corner of the window.
    static void init(int windowW, int windowH);

    // Draw all entities that have Transform + Collider.
    // Call from Engine::render() between glClear and SDL_GL_SwapWindow.
    static void render(EntityManager& em);
};
