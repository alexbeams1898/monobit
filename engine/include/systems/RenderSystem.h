#pragma once

#include "TextureManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// RenderSystem — draws all entities with a Transform + Sprite component.
//
// Uses OpenGL 3.3 core profile: VAO, VBO, vertex + fragment shaders.
// One quad VAO is reused for every sprite — position and UV are set via
// uniforms rather than re-uploading vertex data each frame.
//
// Draw order: sprites are sorted by layer (ascending) before drawing, so
// floor tiles (layer 0) appear behind entities (layer 1+) and UI (layer 10+).
//
// The camera offset (camX, camY) is the world position of the active camera.
// RenderSystem subtracts it from every world position so the view scrolls.
// ---------------------------------------------------------------------------

class RenderSystem
{
  public:
    // Set up shaders, VAO/VBO, and orthographic projection.
    // Must be called once after gladLoadGL succeeds.
    static void init(int windowW, int windowH);

    // Update viewport and projection after window resize.
    static void resize(int windowW, int windowH);

    // Draw all (Transform, Sprite) entities sorted by layer.
    static void render(EntityManager& em, TextureManager& tm, float camX, float camY);

    // Release shader program and VAO/VBO. Call before destroying the GL context.
    static void shutdown();
};
