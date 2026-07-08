#include "GameLoop.h"

#include "Engine.h"
#include "PlayerMovement.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/AnimationSystem.h"
#include "systems/CameraSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"
#include "utils/DirectionUtils.h"

#include <SDL.h>

#include <cmath>

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

    // Facing + walk/idle from the resulting velocity. Moving -> face movement
    // direction (diagonals snap to the dominant cardinal) and animate; still ->
    // hold the current direction's first (standing) frame.
    if (reg.valid(sPlayer))
    {
        if (auto* anim = reg.try_get<Animation>(sPlayer))
        {
            const auto& vel = reg.get<Velocity>(sPlayer);
            const bool moving = (vel.dx != 0.0f || vel.dy != 0.0f);
            if (moving)
            {
                anim->dir = engine::direction::snapMovement(vel.dx, vel.dy, anim->direction_count);
                anim->current_row = 0; // Walk
                anim->current_frames = 4;
                anim->current_duration = 0.14f; // ~7fps
            }
            else
            {
                anim->current_row = 1; // Idle (standing pose, holds last direction)
                anim->current_frames = 1;
                anim->current_duration = 0.0f;
            }
        }
    }

    // Snap the active camera to its entity (the player).
    CameraSystem::update(em);
}

void gamePreRender(Engine& engine, EntityManager& em)
{
    // Sprite-sheet animation advances at wall-clock frame rate, not the fixed
    // tick (see engines/engine/docs/ENGINE.md "Animation system").
    AnimationSystem::update(em, static_cast<float>(engine.frameDt()));
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
