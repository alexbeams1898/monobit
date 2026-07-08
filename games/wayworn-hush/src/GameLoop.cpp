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

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    auto& reg = em.registry();
    const GameState& gs = reg.ctx().get<GameState>();

    // Snapshot positions for render interpolation before integrating.
    for (auto [e, t, pt] : reg.view<Transform, PreviousTransform>().each())
    {
        pt.x = t.x;
        pt.y = t.y;
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const PlayerConfig& pc = gs.player_config;

    // Hold Shift to fast-walk: faster movement + brisker leg cadence.
    const bool fast = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
    const float speed = fast ? pc.speed * pc.run_speed_mult : pc.speed;
    player_movement::update(em, gs.player, keys, speed, static_cast<float>(dt));

    // Facing + state from the resulting velocity. Moving -> face movement
    // direction (diagonals snap to the dominant cardinal) and play walk or
    // fast-walk; still -> hold the standing pose for the last direction.
    auto& anim = reg.get<Animation>(gs.player);
    const auto& vel = reg.get<Velocity>(gs.player);
    if (vel.dx != 0.0f || vel.dy != 0.0f)
    {
        anim.dir = engine::direction::snapMovement(vel.dx, vel.dy, anim.direction_count);
        const PlayerConfig::AnimState& st = fast ? pc.fast_walk : pc.walk;
        anim.current_row = st.row;
        anim.current_frames = st.frames;
        anim.current_duration = st.duration;
    }
    else
    {
        anim.current_row = pc.idle.row;
        anim.current_frames = pc.idle.frames;
        anim.current_duration = pc.idle.duration;
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
