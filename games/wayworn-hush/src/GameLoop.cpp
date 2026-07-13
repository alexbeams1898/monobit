#include "GameLoop.h"

#include "Engine.h"
#include "Footsteps.h"
#include "Glimmer.h"
#include "Notify.h"
#include "PausePage.h"
#include "PlayerMovement.h"
#include "ScreenInput.h"
#include "ThoughtBox.h"
#include "TunePanel.h"
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
#include <random>

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

// The random nudge for observation rolls: a uniform int in [0, n]. Seeded once;
// the observation logic stays testable by taking the RNG as a parameter.
int observeNudge(int n)
{
    static std::mt19937 gen{std::random_device{}()};
    if (n <= 0)
        return 0;
    return std::uniform_int_distribution<int>{0, n}(gen);
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

// Toast color for "new observation / action available" (EXP toasts are emitted
// by the box when the earning line displays -- see ThoughtBox loadLine).
constexpr Color kUnlockColor{0.70f, 0.82f, 0.95f, 1.0f};

// Pre-mark a just-observed spot's already-offered actions as announced (no toast)
// -- they're shown in the menu right there. Only actions that unlock LATER (via
// growth / a flag) toast as a "1 new action" pull-back. Tiers stay un-seeded.
void seedObservedActionsAsKnown(GameState& gs, const std::string& spot)
{
    for (const auto& id : observations::availableUnlocks(gs.observations, gs.growth))
        if (id.rfind(spot + ":", 0) == 0) // "<spot>:<actionid>"
            gs.announced_unlocks.insert(id);
}

// Re-run the engine over the stat keys only when the stats actually changed (a
// cheap sum-of-levels dirty check), so growth can land a thought / re-open a
// miss without a per-frame scan. Fired thoughts queue their own EXP-carrying
// lines; the box toasts the reward when it displays them.
void pumpStatChangeThoughts(GameState& gs)
{
    int statSum = 0;
    for (const auto& [name, level] : gs.growth.stat_levels)
        statSum += level;
    static int sLastStatSum = -1;
    if (statSum == sLastStatSum)
        return;
    sLastStatSum = statSum;
    gs.growth.spirit_exp +=
        observations::evaluateStats(gs.observations, gs.growth, observeNudge).earned;
}

// Conclusions are just deeper observations -- they surface the same way.
void pumpUnlockNotifications(GameState& gs)
{
    const std::unordered_set<std::string> now =
        observations::availableUnlocks(gs.observations, gs.growth);
    for (const auto& id : now)
        if (gs.announced_unlocks.insert(id).second) // first time we've seen it
            notify::push(id.find('@') != std::string::npos ? "1 new observation available"
                                                           : "1 new action available",
                         kUnlockColor);
    // Drop announcements no longer available (consumed), so if the same unlock
    // legitimately re-appears later it announces again.
    for (auto it = gs.announced_unlocks.begin(); it != gs.announced_unlocks.end();)
        it = now.count(*it) ? std::next(it) : gs.announced_unlocks.erase(it);
}

// Observe / read / act input. A reading Line is non-modal (you can keep walking)
// but only Space advances it, so a queued thought is never skipped. Only the
// action MENU is modal (freezes movement + captures F/W/S/Space). With no box,
// Space observes the faced spot, then queues that spot's action menu.
void handleObserveInput(EntityManager& em, GameState& gs, float px, float py, float fx, float fy)
{
    // A queued (not-yet-open) action menu is bound to still facing its spot; if
    // you've walked off before it opened, drop it so it doesn't chase you.
    thought_box::dropQueuedMenuIfLeft(
        observations::facedId(gs.observations, gs.growth, px, py, fx, fy));

    if (thought_box::menuActive())
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
            // A deed can fire a thought -> bank its EXP (the box no longer owns growth).
            gs.growth.spirit_exp += thought_box::confirm(gs.observations, gs.growth, observeNudge);
        if (pressedThisFrame(em, SDL_SCANCODE_W))
            thought_box::moveUp();
        if (pressedThisFrame(em, SDL_SCANCODE_S))
            thought_box::moveDown();
        if (pressedThisFrame(em, SDL_SCANCODE_F))
            thought_box::back();
        return;
    }
    if (thought_box::active()) // a reading Line: Space advances (non-modal, keep walking)
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
            thought_box::confirm(gs.observations, gs.growth, observeNudge);
        return;
    }
    if (!pressedThisFrame(em, SDL_SCANCODE_SPACE))
        return;
    const observations::ObserveResult r =
        observations::observe(gs.observations, gs.growth, px, py, fx, fy, observeNudge);
    gs.growth.spirit_exp += r.earned;
    const std::string spot = observations::facedId(gs.observations, gs.growth, px, py, fx, fy);
    if (!spot.empty())
    {
        // The spot's baseline actions are shown in the menu now -- don't also toast
        // them; only later-unlocked ones announce as a pull-back.
        seedObservedActionsAsKnown(gs, spot);
        thought_box::pushActionMenu(gs.observations, gs.growth, spot);
    }
}
} // namespace

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    auto& reg = em.registry();
    GameState& gs = reg.ctx().get<GameState>();

    // F1 toggles the dev tunables panel (drawn in the ImGui pass).
    if (pressedThisFrame(em, SDL_SCANCODE_F1))
        tune_panel::toggle();

    // M mutes / unmutes the soundtrack (persists across track changes).
    if (pressedThisFrame(em, SDL_SCANCODE_M))
        AudioSystem::toggleMusicMute();

    // A stat change re-runs the engine over the stat keys (growth can land a
    // thought or re-open a prior miss). Then announce any newly-reachable
    // observations/actions (the pull-back to a spot).
    pumpStatChangeThoughts(gs);

    // Cheap + idempotent (diffs against announced_unlocks), so once/frame.
    pumpUnlockNotifications(gs);

    // Only the action MENU is modal (captures F/W/S/Space + freezes movement); a
    // reading never freezes you. So the pause page must not see those keys while a
    // menu is up.
    const bool menuUp = thought_box::menuActive();

    // Pause page controls (F=back/toggle, A/D=tabs, W/S=move, Space=confirm; RMB is
    // back-only so it stays free as a world verb). step() owns the state.
    const bool rmbBack = gs.pause.open && clickedThisFrame(em, SDL_BUTTON_RIGHT);
    const bool toggle = !menuUp && (pressedThisFrame(em, SDL_SCANCODE_F) || rmbBack);
    const bool left = !menuUp && pressedThisFrame(em, SDL_SCANCODE_A);
    const bool right = !menuUp && pressedThisFrame(em, SDL_SCANCODE_D);
    const bool up = !menuUp && pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = !menuUp && pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = !menuUp && pressedThisFrame(em, SDL_SCANCODE_SPACE);
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

    const PlayerConfig& pc = gs.player_config;

    // Only the action menu freezes movement (its W/S drive selection). Readings
    // leave you free to walk -- and walking away dismisses them (below).
    static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {};
    const Uint8* keys = menuUp ? kNoKeys : SDL_GetKeyboardState(nullptr);

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
        // Animation cadence is decoupled from movement speed -- each state plays
        // at its own authored per-frame duration (tune the feel via the duration
        // values; speed and cadence are independent knobs).
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

    // Footstep SFX: a grass footfall on a speed-scaled cadence while moving.
    const bool moving = vel.dx != 0.0f || vel.dy != 0.0f;
    footsteps::update(gs.footstep_state, gs.footstep_config, moving, fast, static_cast<float>(dt));

    // Player position + facing, used by both the glimmer signal and observing.
    const auto& pt = reg.get<Transform>(gs.player);
    float fx = 0.0f;
    float fy = 0.0f;
    facingVector(anim.dir, fx, fy);

    // World glimmer: an unobserved spot glows warm when faced ("come look"); once
    // observed it quiets. Thoughts are not signposted.
    glimmer::update(em, gs.observations, gs.growth, pt.x, pt.y, fx, fy, static_cast<float>(dt));

    // Observe / read / act input. Readings are non-modal (walk while up, Space to
    // advance); only the action menu is modal.
    handleObserveInput(em, gs, pt.x, pt.y, fx, fy);

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
    thought_box::update(gs.observations, gs.growth, static_cast<float>(engine.frameDt()));
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
    // pass runs after the world blit, at window resolution). Tinted by faculty +
    // rarity via the growth state.
    thought_box::render(gs.growth, engine.windowWidth(), engine.windowHeight());

    // Ambient notification toasts (EXP, "new observation/action available") --
    // above the box, non-blocking, self-fading.
    notify::render(static_cast<float>(engine.frameDt()), engine.windowWidth(),
                   engine.windowHeight());

    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const bool lClick = engine::ui::mouseClicked(em, SDL_BUTTON_LEFT);

    // Mouse on the action menu (interchangeable with W/S + Space): hover an option
    // to highlight, click to confirm. Runs after render() stashes menu geometry.
    // The pause page can't be open while the menu is up, so the click is theirs to
    // share without conflict.
    gs.growth.spirit_exp +=
        thought_box::menuMouse(gs.observations, gs.growth, observeNudge, static_cast<float>(mx),
                               static_cast<float>(my), lClick);

    // Pause page over everything (no-op when closed). Mouse is interchangeable
    // with the keyboard controls: hover a tab to highlight, click to switch,
    // click Quit on the System tab to exit. Mouse handling lives here because it
    // hit-tests the geometry render() draws.
    const pause_page::Mouse mouse{static_cast<float>(mx), static_cast<float>(my), lClick};
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

void gameRenderImGui(Engine& /*engine*/, EntityManager& em)
{
    // Dev tunables panel (F1). No-op when hidden. Edits player_config + stats
    // live; the Cognition tab shows the observation state's live tree.
    auto& gs = em.registry().ctx().get<GameState>();
    tune_panel::render(gs.player_config, gs.growth, gs.observations);
}
