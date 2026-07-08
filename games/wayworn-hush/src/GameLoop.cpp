#include "GameLoop.h"

#include "Engine.h"
#include "PlayerMovement.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/CameraSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <SDL.h>

// Player walk speed in world px/sec. ~4 tiles/sec at 32px tiles -- an unhurried
// wander pace (the game is about walking, not rushing). Tunable.
namespace
{
constexpr float kPlayerSpeed = 130.0f;
entt::entity sPlayer = entt::null;
} // namespace

void gameSetPlayer(entt::entity player)
{
    sPlayer = player;
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    auto& reg = em.registry();

    // Snapshot positions for render interpolation before integrating.
    for (auto [e, t, pt] : reg.view<Transform, PreviousTransform>().each())
    {
        pt.x = t.x;
        pt.y = t.y;
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    player_movement::update(em, sPlayer, keys, kPlayerSpeed, static_cast<float>(dt));

    // Snap the active camera to its entity (the player).
    CameraSystem::update(em);
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)alpha;

    engine::gl::pixelTargetBegin(kAmbientR, kAmbientG, kAmbientB);
    // Render at the internal resolution (the pixel target's viewport), zoom 1.
    // camX/camY are the engine-interpolated active-camera position.
    TileMapRenderer::render(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
    RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    (void)engine;
    (void)em;
}
