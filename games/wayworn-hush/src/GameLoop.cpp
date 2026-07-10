#include "GameLoop.h"

#include "Engine.h"
#include "Glimmer.h"
#include "PlayerMovement.h"
#include "ThoughtBox.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/AnimationSystem.h"
#include "systems/AudioSystem.h"
#include "systems/CameraSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"
#include "utils/DirectionUtils.h"

#include <SDL.h>

#include <algorithm>

namespace
{
// Unit facing vector for a cardinal sprite direction (S=0/W=1/E=2/N=3).
void facingVector(CardinalDir dir, float& out_x, float& out_y)
{
    out_x = 0.0f;
    out_y = 0.0f;
    switch (dir)
    {
    case CardinalDir::South:
        out_y = 1.0f;
        break;
    case CardinalDir::West:
        out_x = -1.0f;
        break;
    case CardinalDir::East:
        out_x = 1.0f;
        break;
    case CardinalDir::North:
        out_y = -1.0f;
        break;
    }
}

bool pressedThisFrame(const EntityManager& em, int scancode)
{
    // Edge-triggered: only on the first fixed tick of the frame, and only if the
    // key-down event is buffered this frame (fires once per physical press).
    if (em.ticks_this_frame != 0)
        return false;
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}
} // namespace

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    (void)engine;
    auto& reg = em.registry();
    GameState& gs = reg.ctx().get<GameState>();

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

    // Player position + facing, used by both the glimmer signal and observing.
    const auto& pt = reg.get<Transform>(gs.player);
    float fx = 0.0f;
    float fy = 0.0f;
    facingVector(anim.dir, fx, fy);

    // World glimmer: the observable the player faces softly brightens (the
    // "you can notice this" signal). Fades otherwise.
    glimmer::update(em, gs.observations, pt.x, pt.y, fx, fy, static_cast<float>(dt));

    // Observe (Space): notice the observable the player faces. Deeper tiers +
    // formed conclusions surface a thought and grant currency; a soft sound
    // marks the act (placeholder -- final audio from the OST, see design doc).
    if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
    {
        const observations::Outcome oc = observations::observe(gs.observations, pt.x, pt.y, fx, fy);
        if (oc != observations::Outcome::None)
            AudioSystem::playSfx("assets/audio/observe.ogg", 0.7f);
    }

    // Snap the active camera to its entity (the player).
    CameraSystem::update(em);
}

void gamePreRender(Engine& engine, EntityManager& em)
{
    // Sprite-sheet animation advances at wall-clock frame rate, not the fixed
    // tick (see engines/engine/docs/ENGINE.md "Animation system").
    AnimationSystem::update(em, static_cast<float>(engine.frameDt()));

    auto& gs = em.registry().ctx().get<GameState>();
    thought_box::update(gs.observations, static_cast<float>(engine.frameDt()));
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
    // Inner-monologue textbox, drawn in native window space (the engine's UI
    // pass runs after the world blit, at window resolution).
    thought_box::render(engine.windowWidth(), engine.windowHeight());

    // Clear one-shot input buffers after all consumers have seen them (the
    // engine fills them but leaves clearing to the game). Guard on a tick having
    // run so events arriving on a 0-tick frame aren't discarded before the
    // fixed-step observe/input code reads them.
    if (em.ticks_this_frame > 0)
    {
        em.key_down_events.clear();
        em.mouse_down_events.clear();
        em.mouse_wheel_y = 0;
        em.text_input_buffer.clear();
    }
}
