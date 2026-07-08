#include "GameLoop.h"

#include "Engine.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/TileMapRenderer.h"

// Slice stage: the world-render callback binds the internal-res pixel target,
// draws the tilemap into it, and blits it up to the window. No player/camera
// entity yet, so the view is centered on the region middle. RenderSystem
// (sprites) and a follow camera drop in with the player step.

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    (void)em;
    (void)dt;
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)camX;
    (void)camY;
    (void)alpha;

    // No camera entity yet: center the view on the region's middle.
    const float centerX = static_cast<float>(em.tile_map.width * em.tile_map.tile_size) * 0.5f;
    const float centerY = static_cast<float>(em.tile_map.height * em.tile_map.tile_size) * 0.5f;

    engine::gl::pixelTargetBegin(kAmbientR, kAmbientG, kAmbientB);
    // Render at the internal resolution (the pixel target's viewport), zoom 1.
    TileMapRenderer::render(centerX, centerY, kInternalWidth, kInternalHeight, 1.0f);
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    (void)engine;
    (void)em;
}
