#include "GameLoop.h"

#include "Engine.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"

// Slice stage: no world content or systems yet. The world-render callback binds
// the internal-res pixel target, clears it, draws nothing, and blits it up to
// the window. Tilemap, player, and render passes drop into the marked span in
// later slice steps.

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    (void)em;
    (void)dt;
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)em;
    (void)camX;
    (void)camY;
    (void)alpha;

    engine::gl::pixelTargetBegin(kAmbientR, kAmbientG, kAmbientB);
    // --- world draws here (TileMapRenderer + RenderSystem) in later steps ---
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    (void)engine;
    (void)em;
}
