#include "GameLoop.h"

#include "Engine.h"
#include "ecs/EntityManager.h"

// Scaffold stage: no world, no systems yet. The engine clears the framebuffer
// to the ambient color and swaps; these callbacks intentionally do nothing
// until the tilemap, player, and render passes land in later slice steps.

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    (void)em;
    (void)dt;
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)engine;
    (void)em;
    (void)camX;
    (void)camY;
    (void)alpha;
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    (void)engine;
    (void)em;
}
