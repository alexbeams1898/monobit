#include "GameLoop.h"

#include "Engine.h"
#include "Glimmer.h"
#include "PausePage.h"
#include "PlayerMovement.h"
#include "ScreenInput.h"
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
#include <cstdint>

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

// Edge-triggered mouse-button press, same first-tick guard as pressedThisFrame
// so a multi-tick frame can't fire it twice. Consumes the click (via the engine
// consume flag) so gameplay doesn't also act on it.
bool clickedThisFrame(EntityManager& em, uint8_t button)
{
    if (em.ticks_this_frame != 0)
        return false;
    return engine::ui::mouseClicked(em, button);
}
} // namespace

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    auto& reg = em.registry();
    GameState& gs = reg.ctx().get<GameState>();

    // Pause page. Controls stay in the left-hand WASD cluster (no Esc): F is the
    // universal back/no button and toggles the page; A/D page the tabs; W/S move
    // the selected item; Space is the universal yes/confirm. RMB is the mouse
    // equivalent of the back/no button (studio standard). Edge-triggered; the
    // pure step() owns state.
    // RMB is back-only (it closes/pops within the GUI, never opens it -- so it
    // stays free as a world verb); F opens and closes.
    const bool rmbBack = gs.pause.open && clickedThisFrame(em, SDL_BUTTON_RIGHT);
    const bool toggle = pressedThisFrame(em, SDL_SCANCODE_F) || rmbBack;
    const bool left = pressedThisFrame(em, SDL_SCANCODE_A);
    const bool right = pressedThisFrame(em, SDL_SCANCODE_D);
    const bool up = pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = pressedThisFrame(em, SDL_SCANCODE_SPACE);
    const pause_page::Action action =
        pause_page::step(gs.pause, toggle, left, right, up, down, confirm);
    if (action == pause_page::Action::Quit)
        engine.requestQuit();

    // Muffle the soundtrack while the page is open (the world is frozen behind a
    // held breath); clean when closed. Cheap no-op when unchanged.
    AudioSystem::setMusicLowPass(gs.pause.open ? 800.0f : 0.0f);

    // Freeze the world while the page is open: skip movement, observing, camera.
    // Animation is halted in gamePreRender (it runs at wall-clock rate there).
    if (gs.pause.open)
        return;

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
    // formed conclusions surface a thought and earn Spirit EXP (added to the
    // growth economy, the sole owner of the total); a soft sound marks the act
    // (placeholder -- final audio from the OST, see design doc).
    if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
    {
        const observations::ObserveResult r =
            observations::observe(gs.observations, pt.x, pt.y, fx, fy);
        gs.growth.spirit_exp += r.earned;
        if (r.outcome != observations::Outcome::None)
            AudioSystem::playSfx("assets/audio/observe.ogg", 0.7f);
    }

    // Snap the active camera to its entity (the player).
    CameraSystem::update(em);
}

void gamePreRender(Engine& engine, EntityManager& em)
{
    auto& gs = em.registry().ctx().get<GameState>();

    // The page freezes the world: hold the sprite pose and stop draining
    // monologue lines (both advance at wall-clock rate, so they must be gated
    // here rather than in the fixed-step update).
    if (gs.pause.open)
        return;

    // Sprite-sheet animation advances at wall-clock frame rate, not the fixed
    // tick (see engines/engine/docs/ENGINE.md "Animation system").
    AnimationSystem::update(em, static_cast<float>(engine.frameDt()));
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
    auto& gs = em.registry().ctx().get<GameState>();

    // Inner-monologue textbox, drawn in native window space (the engine's UI
    // pass runs after the world blit, at window resolution).
    thought_box::render(engine.windowWidth(), engine.windowHeight());

    // Pause page over everything (no-op when closed). Mouse is interchangeable
    // with the keyboard controls: hover a tab to highlight, click to switch,
    // click Quit on the System tab to exit. Mouse handling lives here because it
    // hit-tests the geometry render() draws.
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const pause_page::Mouse mouse{static_cast<float>(mx), static_cast<float>(my),
                                  engine::ui::mouseClicked(em, SDL_BUTTON_LEFT)};
    if (pause_page::render(gs.pause, gs.growth, gs.observations, mouse, engine.windowWidth(),
                           engine.windowHeight()) == pause_page::Action::Quit)
        engine.requestQuit();

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
