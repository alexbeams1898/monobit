#include "GameLoop.h"

#include "Engine.h"
#include "Footsteps.h"
#include "Glimmer.h"
#include "Interaction.h"
#include "LoadScreen.h"
#include "NameScreen.h"
#include "Notify.h"
#include "PausePage.h"
#include "PlayerMovement.h"
#include "ReadingColor.h"
#include "SaveGame.h"
#include "ScreenInput.h"
#include "ScreenToWorld.h"
#include "SettingsScreen.h"
#include "ThoughtBox.h"
#include "TitleScreen.h"
#include "TunePanel.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/AnimationSystem.h"
#include "systems/AudioSystem.h"
#include "systems/CameraSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <random>

namespace
{
// Forward decls -- helpers below reference these before their definitions appear.
bool pressedThisFrame(const EntityManager& em, int scancode);
bool clickedThisFrame(EntityManager& em, uint8_t button);
void enactConfirm(EntityManager& em, GameState& gs, const thought_box::ConfirmResult& r);
void attemptCraft(GameState& gs);
void enactPageAction(Engine& engine, EntityManager& em, GameState& gs, pause_page::Action action);

// The surface name of the tile at world (wx,wy): the tile's id looked up in the
// region's tile->surface map. Empty if off-map or the tile is untagged (footsteps then
// use the default pool). Drives per-surface footfalls.
std::string surfaceUnder(const EntityManager& em, const GameState& gs, float wx, float wy)
{
    const TileMap& tm = em.tile_map;
    if (!tm.valid())
        return {};
    const int col = static_cast<int>(wx) / tm.tile_size;
    const int row = static_cast<int>(wy) / tm.tile_size;
    if (!tm.in_bounds(col, row))
        return {};
    // Per-cell override first (a bridge deck sounds like wood even over water); then the
    // tile-type surface (grass/water/sand) keyed by the tile id under the player.
    const std::size_t idx = tm.cellIndex(col, row);
    if (const auto cit = gs.cell_surface.find(idx); cit != gs.cell_surface.end())
        return cit->second;
    const auto it = gs.tile_surface.find(tm.at(col, row).tile_id);
    return it == gs.tile_surface.end() ? std::string{} : it->second;
}

// "Back" this frame, however the player said it: F, Escape, or a right-click. ONE
// definition, because a back that works on one surface and not the next is worse than no
// back at all -- the player learns the gesture on the pause page and then finds it dead in
// settings. Every screen asks here rather than deciding for itself.
bool backPressed(EntityManager& em)
{
    return pressedThisFrame(em, SDL_SCANCODE_F) || pressedThisFrame(em, SDL_SCANCODE_ESCAPE) ||
           clickedThisFrame(em, SDL_BUTTON_RIGHT);
}

// Step the pause page from this frame's input, returning its action. F/Esc/RMB=back-toggle,
// A/D=tabs, W/S=move, Space=confirm. The action menu (menuUp) is modal and captures those
// keys, so the page ignores input while a menu is up. Also muffles the soundtrack while the
// page is open.
pause_page::Action stepPausePage(EntityManager& em, GameState& gs, bool menuUp)
{
    // RMB only BACKS OUT of an open page -- it must not open one, staying free as a world
    // verb (right-clicking the world is not a request for a menu).
    const bool back = gs.pause.open ? backPressed(em) : pressedThisFrame(em, SDL_SCANCODE_F);
    const bool toggle = !menuUp && back;
    const bool left = !menuUp && pressedThisFrame(em, SDL_SCANCODE_A);
    const bool right = !menuUp && pressedThisFrame(em, SDL_SCANCODE_D);
    const bool up = !menuUp && pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = !menuUp && pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = !menuUp && pressedThisFrame(em, SDL_SCANCODE_SPACE);
    const std::vector<pause_page::CraftMaterial> craftMats =
        pause_page::craftMaterials(gs.satchel, gs.items);
    // Returns the keyboard action for the caller to enact (via enactPageAction) -- this function
    // gathers input + owns the page's side-concerns (badges, audio) but does NOT dispatch actions,
    // so keyboard + mouse share the one dispatch point.
    const pause_page::Action action =
        pause_page::step(gs.pause, toggle, left, right, up, down, confirm, craftMats);

    // "New" item badges clear when the player LEAVES the satchel view (switched tab or closed
    // the page while on it) -- they saw the fresh finds, so they're no longer new.
    const bool viewingSatchel = gs.pause.open && gs.pause.tab == PauseState::Tab::Satchel;
    static bool sWasViewingSatchel = false;
    if (sWasViewingSatchel && !viewingSatchel)
        inventory::markAllSeen(gs.satchel);
    sWasViewingSatchel = viewingSatchel;

    // Muffle the soundtrack while the page is open (world frozen behind a held breath).
    AudioSystem::setMusicLowPass(gs.pause.open ? 800.0f : 0.0f);
    return action;
}

// Pick the player's animation CLIP: walk/fast-walk while moving, idle at rest. Facing is not
// set here -- FacingDirection carries it and AnimationSystem snaps the cardinal (see spawn).
void updatePlayerAnim(Animation& anim, bool moving, const PlayerConfig& pc, bool fast)
{
    const PlayerConfig::AnimState& st = moving ? (fast ? pc.fast_walk : pc.walk) : pc.idle;
    anim.current_row = st.row;
    anim.current_frames = st.frames;
    anim.current_duration = st.duration;
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

// Toast color for a craft that didn't take (no match / near-miss / short on materials) -- a
// muted grey, quieter than a made-item toast so a failed attempt doesn't read as an achievement.
constexpr Color kCraftMissColor{0.72f, 0.72f, 0.70f, 1.0f};

// The autosave throttle: world seconds that must pass between writes driven by
// progress. Leaving the page or the game always writes regardless.
constexpr double kAutosaveThrottleSecs = 30.0;

// Installed by main (see setWorldEnter/setRegionSwitch): the region/spawn plumbing
// lives there, so the loop calls back rather than owning the map pipeline.
WorldEnterFn sWorldEnter = nullptr;
RegionSwitchFn sRegionSwitch = nullptr;

// Authoring overlay: draw every warp box + the player's body box where the game
// actually computes them (through the same blit mapping the mouse uses). Toggled
// with F2 in the world; off by default.
bool sShowWarpDebug = false;

// A warp crossed last tick swaps the world at a SAFE point -- the top of the update,
// before any system holds references into the registry the switch clears. On failure
// (bad target level) the current region is still standing: log and walk on.
void applyPendingWarp(Engine& engine, EntityManager& em, GameState& gs)
{
    if (!gs.pending_warp.active)
        return;
    const auto pw = gs.pending_warp;
    gs.pending_warp = {};
    if (sRegionSwitch != nullptr && !sRegionSwitch(engine, em, gs, pw.level, pw.spawn))
        std::fprintf(stderr, "[region] warp to '%s' failed -- staying put\n", pw.level.c_str());
}

// Walk the pending warp through its fade: a crossing starts Out, the region swaps the
// moment Out completes (full black -- the player never sees the swap), and In runs in
// the new place until the screen is lit again. A zero duration skips the fade
// entirely. A failed swap (applyPendingWarp logs it) still fades back in, standing
// where you were.
void tickWarpFade(Engine& engine, EntityManager& em, GameState& gs, double dt)
{
    auto& fade = gs.warp_fade;
    const float dur = gs.world_config.warp_fade_seconds;
    if (fade.phase == GameState::WarpFade::Phase::None)
    {
        if (!gs.pending_warp.active)
            return;
        // The door sounds as the crossing begins: leaving an interior = exit, else enter.
        const std::string& sfx =
            gs.region_interior ? gs.world_config.warp_exit_sfx : gs.world_config.warp_enter_sfx;
        if (!sfx.empty())
            AudioSystem::playSfx(sfx, gs.world_config.warp_sfx_volume);
        if (dur <= 0.0f)
        {
            applyPendingWarp(engine, em, gs);
            return;
        }
        fade.phase = GameState::WarpFade::Phase::Out;
        fade.t = 0.0f;
        return;
    }
    fade.t += static_cast<float>(dt);
    if (fade.t < dur)
        return;
    if (fade.phase == GameState::WarpFade::Phase::Out)
    {
        applyPendingWarp(engine, em, gs);
        fade.phase = GameState::WarpFade::Phase::In;
        fade.t = 0.0f;
    }
    else
    {
        fade = {};
    }
}

// The world's global toggles: F1 = dev tunables panel (ImGui pass), F2 = warp-box
// diagnostic overlay, M = soundtrack mute (persists across track changes).
void handleWorldHotkeys(EntityManager& em)
{
    if (pressedThisFrame(em, SDL_SCANCODE_F1))
        tune_panel::toggle();
    if (pressedThisFrame(em, SDL_SCANCODE_F2))
        sShowWarpDebug = !sShowWarpDebug;
    if (pressedThisFrame(em, SDL_SCANCODE_M))
        AudioSystem::toggleMusicMute();
}

// How dark the warp-fade overlay is right now: 0 (no fade running) -> 1 (full black
// at the swap) -> 0. The render pass draws a black rect at this alpha over everything.
float warpFadeAlpha(const GameState& gs)
{
    const float dur = gs.world_config.warp_fade_seconds;
    if (gs.warp_fade.phase == GameState::WarpFade::Phase::None || dur <= 0.0f)
        return 0.0f;
    const float a = std::clamp(gs.warp_fade.t / dur, 0.0f, 1.0f);
    return gs.warp_fade.phase == GameState::WarpFade::Phase::Out ? a : 1.0f - a;
}

// A warp is a directional THRESHOLD: a thin strip you cross against its facing.
// `facing` is the way you step out when arriving here, so entering is always the
// opposite push -- the mat at a door's base faces north (out into the room) and fires
// when you push south across it; the strip at a building's door faces south and fires
// when you press north into the doorway. One rule for every passage:
//
//     fire = the body's INTENDED path this tick touches the strip  AND
//            intent pushes against the strip's facing
//
// The intended path (tick start -> current + intent carried to a full stride, a swept
// segment ignoring collision) is what makes thin strips both speed-proof and
// pixel-proof: a fast tick can step clean over a 4px band but a segment cannot skip a
// box it crossed, and a wall can stop your feet a fraction of a pixel short of a strip
// but it cannot stop where you were headed. Standing still degenerates to plain
// overlap, so pressing into a door (stopped by the building, zero motion) still
// counts. Crossing sideways or brushing past without pushing in fires nothing.
// `touching` holds the re-arm latch.
struct WarpProbe
{
    float prevx = 0.0f, prevy = 0.0f; // body center at the tick's start
    float px = 0.0f, py = 0.0f;       // INTENDED body center (current + intent stride)
    float hw = 0.0f, hh = 0.0f;       // body half extents
    float ix = 0.0f, iy = 0.0f;       // movement intent
};

bool warpFires(const ldtk::WarpPlacement& w, const WarpProbe& p, bool& touching)
{
    // The strip expanded by the body's half extents: the box the body's CENTER must
    // enter for the body to touch the strip. Segment-vs-box (slab test) against it.
    const float ox = w.w * 0.5f + p.hw;
    const float oy = w.h * 0.5f + p.hh;
    const float dx = p.px - p.prevx;
    const float dy = p.py - p.prevy;
    float t0 = 0.0f;
    float t1 = 1.0f;
    const auto slab = [&](float pos, float d, float lo, float hi)
    {
        if (d == 0.0f)
            return pos >= lo && pos <= hi; // no motion on this axis: must already be inside
        const float a = (lo - pos) / d;
        const float b = (hi - pos) / d;
        t0 = std::max(t0, std::min(a, b));
        t1 = std::min(t1, std::max(a, b));
        return t0 <= t1;
    };
    const bool crossed = slab(p.prevx, dx, w.x - ox, w.x + ox) && //
                         slab(p.prevy, dy, w.y - oy, w.y + oy);
    if (!crossed)
        return false;
    touching = true;
    float fx = 0.0f;
    float fy = 0.0f;
    ldtk::facingVec(w.facing, fx, fy);
    return p.ix * fx + p.iy * fy < 0.0f; // pushing AGAINST the facing = crossing in
}

void detectWarpCrossing(EntityManager& em, GameState& gs, float px, float py, float ix, float iy,
                        float stride)
{
    float hw = 0.0f;
    float hh = 0.0f;
    if (const auto* col = em.registry().try_get<Collider>(gs.player))
    {
        hw = col->width * 0.5f;
        hh = col->height * 0.5f;
    }
    WarpProbe probe;
    probe.px = px + ix * stride;
    probe.py = py + iy * stride;
    probe.prevx = px;
    probe.prevy = py;
    probe.hw = hw;
    probe.hh = hh;
    probe.ix = ix;
    probe.iy = iy;
    // Where this tick's movement started -- the engine snapshots it at the tick top, so
    // (prev -> current) is exactly the path travelled this tick.
    if (const auto* prev = em.registry().try_get<PreviousTransform>(gs.player))
    {
        probe.prevx = prev->x;
        probe.prevy = prev->y;
    }
    bool touching = false;
    for (const auto& w : gs.region_warps)
    {
        const bool fires = warpFires(w, probe, touching);
        if (fires && gs.warp_armed && !gs.pending_warp.active)
        {
            gs.pending_warp = {true, w.target_level, w.target};
            break;
        }
    }
    if (!touching)
        gs.warp_armed = true;
}

// Note that something worth keeping just happened. Called ONLY from the deed / craft /
// grant / reading chokepoints -- deliberately NOT from the clock, which moves every
// frame and would make "has anything changed?" always true. The autosave reads this.
void markProgress(GameState& gs)
{
    ++gs.progress_events;
}

// Write down thoughts that just landed. The notebook keeps only WHEN each one came --
// which thoughts he's had is already the observation record's job, and what they're
// worth is the thought's own. Writing at all needs the notebook in hand; GameLoop owns
// that item knowledge, which is why the note happens here rather than where the thought
// fired.
//
// The moment is ALWAYS recorded. The world's time runs whether or not he can read it --
// the watch is an instrument, not the clock -- so whether a note's time can be TOLD is a
// question for whoever displays it, never a reason to lose it here.
void noteLanded(GameState& gs, const std::vector<std::string>& landed)
{
    if (landed.empty() || !inventory::has(gs.satchel, "notebook"))
        return;
    // Thoughts are written; remarks are said. A remark never reaches the
    // notebook -- it left through a mouth instead (Thought::isRemark is the one
    // authority; notebook::found applies the same test on display).
    const auto isRemark = [&](const std::string& id)
    {
        for (const auto& t : gs.psyche.thoughts)
            if (t.id == id)
                return t.isRemark();
        return false;
    };
    for (const auto& id : landed)
        if (!isRemark(id))
            notebook::note(gs.notebook, id, gs.clock.seconds);
}

// Bank everything an observe/act result earned: Spirit currency, faculty EXP (passive stat
// growth), and the notebook note. The ONE place a cognition result is applied, so the five
// call sites (observe, ambient, deed confirm, deed take, stat re-check) can't drift on which
// rewards they remember to grant. Templated over the result type -- ObserveResult and
// ConfirmResult both carry earned / stat_gains / landed.
// Grow a stat by exp AND toast it -- the ONE hook every exp source flows through, so a new
// source (a deed, crafting, a tree node) gets the notification for free. Color is the stat's
// own hue; the toast reads "+N <Stat>". Capitalizes the first letter for display.
void grantStatExp(GameState& gs, const std::string& stat, int exp)
{
    if (stat.empty() || exp <= 0)
        return;
    growth::recordUse(gs.growth, stat, exp);
    std::string label = stat;
    if (!label.empty())
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    const growth::Rgb hue = growth::facultyColor(gs.growth, stat);
    notify::push("+" + std::to_string(exp) + " " + label, {hue.r, hue.g, hue.b, 1.0f});
}

template <typename Result> void applyGains(GameState& gs, const Result& r)
{
    gs.growth.spirit_exp += r.earned;
    for (const auto& [stat, exp] : r.stat_gains)
        grantStatExp(gs, stat, exp);
    noteLanded(gs, r.landed);
}

// Drop this frame's one-shot input after every consumer has seen it (the engine fills the
// buffers but leaves clearing to the game). Guarded on a tick having run so events arriving
// on a 0-tick frame aren't discarded before the fixed-step code reads them.
//
// EVERY path out of the UI pass must call this: a path that skips it leaves the press in
// the buffer to be re-read on every later frame (a held-down key that never lifts).
void clearOneShotInput(EntityManager& em)
{
    if (em.ticks_this_frame > 0)
    {
        em.key_down_events.clear();
        em.mouse_down_events.clear();
        em.mouse_wheel_y = 0;
        em.text_input_buffer.clear();
    }
}

// Walk as the pilgrim `id`: build their world and step into it. The one path from any
// menu into play. Leaving is the only honest response to a world that won't build.
void walkAs(Engine& engine, EntityManager& em, GameState& gs, const std::string& id)
{
    if (sWorldEnter == nullptr)
        return;
    if (sWorldEnter(engine, em, gs, id))
    {
        gs.app.world_built = true;
        gs.app.phase = app::Phase::Playing;

        // Building the world is a heavy synchronous stretch in the middle of a tick. Left
        // alone, the accumulator has banked all of it and fires a burst of catch-up ticks
        // the moment the world appears -- the map lurches instead of simply being there.
        engine.requestTimingReset();
    }
    else
    {
        // The region IS the map -- if it won't load there is no world to enter, and
        // sitting on the title forever is a lie. Leave rather than pretend.
        std::fprintf(stderr, "[app] could not build the world; leaving\n");
        engine.requestQuit();
    }
}

// The roster as the load screen wants it: who they are, and how far they got.
std::vector<load_screen::Entry> rosterEntries(const savegame::File& file, const GameState& gs)
{
    std::vector<load_screen::Entry> out;
    out.reserve(file.pilgrims.size());
    for (const auto& p : file.pilgrims)
        out.push_back(load_screen::Entry{p.id, p.name, worldclock::dayAt(gs.clock, p.clock_seconds),
                                         p.place.walked});
    return out;
}

// Put the world away and go back to the title. The walk is already saved by the caller.
//
// Everything the world put in the registry is destroyed rather than reset: the next walk
// builds a fresh one from the map (see enterWorld), so a leftover entity would be a ghost
// from someone else's pilgrimage.
//
// The per-walk STATE is not cleared field-by-field here -- entering a world rebuilds it
// from the authored config and then applies that pilgrim's walk onto it (see enterWorld).
// A hand-written clear list would be one more thing to remember every time GameState grows
// a field, and forgetting one leaks a stranger's progress into the next walk.
void leaveToTitle(EntityManager& em, GameState& gs)
{
    em.registry().clear(); // every world entity: player, props, glimmers, items, markers
    gs.player = entt::null;

    // Ephemeral surfaces that outlive the world they belonged to.
    gs.pause = PauseState{};
    gs.progress_events = 0;
    gs.saved_at_events = 0;
    gs.since_save_secs = 0.0;
    gs.app.active_id.clear();
    gs.app.world_built = false;

    AudioSystem::stopMusic();
    // The world's sounds and its running scene are the walk's, not the title's --
    // a quit mid-scene must not leave an alarm ringing over the menu, or a scene
    // runtime pointing into a registry the next walk reloads.
    ambience::stopAll(gs.ambience_state, gs.ambience_config);
    gs.scene_rt = {};
    thought_box::reset();
    title_screen::reset();
    gs.app.phase = app::Phase::Greeting;
}

// Enact what the title committed. The ONE place a title action turns into something --
// both its input paths (keys via step, mouse via render) funnel here, so a new entry is
// wired in one spot and neither path can drop it (the same rule enactPageAction follows).
void enactTitleAction(Engine& engine, EntityManager& em, GameState& gs, title_screen::Action action)
{
    (void)em;
    switch (action)
    {
    case title_screen::Action::NewGame:
        name_screen::reset();
        // Without this the platform sends no text events and the field stays empty no
        // matter what is typed. Stopped again on every way out of the phase.
        SDL_StartTextInput();
        gs.app.phase = app::Phase::Naming;
        break;
    case title_screen::Action::LoadGame:
        load_screen::reset();
        gs.app.phase = app::Phase::Loading;
        break;
    case title_screen::Action::Settings:
        settings_screen::reset();
        gs.app.settings_return_to = app::Phase::Greeting;
        gs.app.phase = app::Phase::Settings;
        break;
    case title_screen::Action::Quit:
        engine.requestQuit();
        break;
    case title_screen::Action::None:
        break;
    }
}

// The greeting: read the keys, let the title report what was chosen, enact it.
void updateGreeting(Engine& engine, EntityManager& em, GameState& gs)
{
    const bool up = pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = pressedThisFrame(em, SDL_SCANCODE_SPACE);
    enactTitleAction(engine, em, gs,
                     title_screen::step(up, down, confirm, (gs.app.pilgrim_count > 0)));
}

// Enact what the name screen committed. The ONE place a naming action turns into
// something -- both its input paths (keys via step, buttons via render) funnel here.
void enactNameAction(Engine& engine, EntityManager& em, GameState& gs, name_screen::Action action)
{
    if (action == name_screen::Action::None)
        return;

    // Every way out of the field stops text input -- leaving it on would keep the platform
    // sending text events into a phase that has no field to put them in.
    SDL_StopTextInput();

    if (action == name_screen::Action::Back)
    {
        title_screen::reset();
        gs.app.phase = app::Phase::Greeting;
        return;
    }

    // The pilgrim is created on disk BEFORE the world is built: one the game is walking as
    // must exist to be saved into.
    savegame::File file = savegame::load();
    const std::string id = savegame::add(file, name_screen::name());
    if (!savegame::save(file))
    {
        // A pilgrim the game can't save into is a walk that evaporates. Stay on the field
        // rather than pretend.
        SDL_StartTextInput();
        return;
    }
    gs.app.pilgrim_count = static_cast<int>(file.pilgrims.size());
    walkAs(engine, em, gs, id);
}

// Naming a new pilgrim: gather this frame's keys and let the screen decide.
void updateNaming(Engine& engine, EntityManager& em, GameState& gs, double dt)
{
    name_screen::Keys keys;
    keys.typed = em.text_input_buffer;
    // The edit keys are HELD states (the field's key-repeat needs the hold); the commit
    // keys are edges (one press, one action).
    const Uint8* held = SDL_GetKeyboardState(nullptr);
    keys.backspace = held[SDL_SCANCODE_BACKSPACE] != 0;
    keys.del = held[SDL_SCANCODE_DELETE] != 0;
    keys.left = pressedThisFrame(em, SDL_SCANCODE_LEFT);
    keys.right = pressedThisFrame(em, SDL_SCANCODE_RIGHT);
    keys.home = pressedThisFrame(em, SDL_SCANCODE_HOME);
    keys.end = pressedThisFrame(em, SDL_SCANCODE_END);
    keys.confirm = pressedThisFrame(em, SDL_SCANCODE_RETURN);
    // NOT backPressed(): this screen is typing a name, and F is a letter -- the shared
    // back-gesture would abort the moment the player typed one. Escape and RMB only.
    keys.back = pressedThisFrame(em, SDL_SCANCODE_ESCAPE) || clickedThisFrame(em, SDL_BUTTON_RIGHT);

    enactNameAction(engine, em, gs, name_screen::step(keys, dt));
}

// Enact what the roster committed. The ONE place a load action turns into something --
// both its input paths (keys via step, buttons via render) funnel here, so neither can
// silently drop one (the rule enactPageAction / enactTitleAction follow).
void enactLoadAction(Engine& engine, EntityManager& em, GameState& gs, load_screen::Action action)
{
    switch (action)
    {
    case load_screen::Action::Walk:
        walkAs(engine, em, gs, load_screen::id());
        break;
    case load_screen::Action::Forget:
    {
        // Re-read rather than trust a roster gathered earlier this frame: forgetting must
        // act on what is actually on disk.
        savegame::File file = savegame::load();
        savegame::remove(file, load_screen::id());
        if (savegame::save(file))
            gs.app.pilgrim_count = static_cast<int>(file.pilgrims.size());
        // Forgetting the LAST pilgrim empties the roster, and a roster of nobody is not a
        // screen -- there is nothing there to choose. Leave for the title, which shows Load
        // Game disabled for the same reason.
        if (gs.app.pilgrim_count == 0)
        {
            title_screen::reset();
            gs.app.phase = app::Phase::Greeting;
        }
        break;
    }
    case load_screen::Action::Back:
        title_screen::reset();
        gs.app.phase = app::Phase::Greeting;
        break;
    case load_screen::Action::None:
        break;
    }
}

// Leaving settings: keep the preferences and go back where they were opened from.
//
// Its own write, not writeSave's: a preference belongs to the INSTALLATION, so it has to
// persist from the title too -- where writeSave does nothing, there being no walk to keep.
// Reads the file fresh and puts back only `prefs`, so this can't clobber a roster.
void enactSettingsAction(GameState& gs, settings_screen::Action action)
{
    if (action != settings_screen::Action::Back)
        return;
    savegame::File file = savegame::load();
    file.prefs = gs.prefs;
    savegame::save(file);
    gs.app.phase = gs.app.settings_return_to;
    if (gs.app.phase == app::Phase::Greeting)
        title_screen::reset();
}

// Settings: W/S move, Space opens a category, A/D change the highlighted setting, and back
// (F / Esc / RMB) steps out a level -- out of a category, then out of the screen entirely,
// to wherever it was opened from. Changes land in gs.prefs as they're made; the write
// happens on the way out.
// Takes em by non-const ref: reading the back gesture CONSUMES a right-click (so the
// world underneath never also sees it).
void updateSettings(EntityManager& em, GameState& gs)
{
    const bool up = pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = pressedThisFrame(em, SDL_SCANCODE_S);
    const bool left = pressedThisFrame(em, SDL_SCANCODE_A);
    const bool right = pressedThisFrame(em, SDL_SCANCODE_D);
    const bool confirm = pressedThisFrame(em, SDL_SCANCODE_SPACE);
    enactSettingsAction(
        gs, settings_screen::step(gs.prefs, up, down, left, right, confirm, backPressed(em)));
}

// The roster: walk as someone, forget someone, or go back.
void updateLoading(Engine& engine, EntityManager& em, GameState& gs)
{
    const savegame::File file = savegame::load();
    const std::vector<load_screen::Entry> entries = rosterEntries(file, gs);

    const bool up = pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = pressedThisFrame(em, SDL_SCANCODE_RETURN);
    const bool forget = pressedThisFrame(em, SDL_SCANCODE_DELETE);
    const bool back = backPressed(em);

    enactLoadAction(engine, em, gs, load_screen::step(entries, up, down, confirm, forget, back));
}

// Is the app in a menu rather than the world? A menu owns the whole screen and no world
// ticks under it. For the title/naming/roster there is no world to tick; for Settings --
// which can be opened from a walk -- there is one, and it holds exactly as it does behind
// the pause page. Either way the world doesn't move while a menu is up.
bool inMenu(const GameState& gs)
{
    return gs.app.phase != app::Phase::Playing;
}

// Step whichever menu is up. Split out of gameUpdate so the world tick isn't carrying the
// menus' branches around with it.
void updateMenu(Engine& engine, EntityManager& em, GameState& gs, double dt)
{
    switch (gs.app.phase)
    {
    case app::Phase::Greeting:
        updateGreeting(engine, em, gs);
        break;
    case app::Phase::Naming:
        updateNaming(engine, em, gs, dt);
        break;
    case app::Phase::Loading:
        updateLoading(engine, em, gs);
        break;
    case app::Phase::Settings:
        updateSettings(em, gs);
        break;
    case app::Phase::Playing:
        break; // not a menu
    }
}

// Write the active pilgrim's walk now. Reads the roster, updates only that pilgrim, and
// writes the file back -- so a save can never clobber someone else's walk. No-op when
// nobody is walking (the title) or the pilgrim has been forgotten.
void writeSave(const EntityManager& em, GameState& gs)
{
    if (gs.app.active_id.empty())
        return;

    savegame::File file = savegame::load();
    savegame::Data* pilgrim = savegame::find(file, gs.app.active_id);
    if (pilgrim == nullptr)
        return;

    float px = 0.0f;
    float py = 0.0f;
    if (const auto* t = em.registry().try_get<Transform>(gs.player))
    {
        px = t->x;
        py = t->y;
    }
    savegame::capture(gs, px, py, *pilgrim);
    if (savegame::save(file))
    {
        gs.saved_at_events = gs.progress_events;
        gs.since_save_secs = 0.0;
    }
}

// Write only if something worth keeping has happened since the last write AND the
// throttle has elapsed -- so a busy stretch doesn't write every frame, and a quiet
// walk doesn't rewrite an unchanged save.
void autosaveTick(const EntityManager& em, GameState& gs, double dt)
{
    gs.since_save_secs += dt;
    if (gs.progress_events != gs.saved_at_events && gs.since_save_secs >= kAutosaveThrottleSecs)
        writeSave(em, gs);
}

// Re-run the engine over the stat keys only when the stats actually changed (the
// levelSum fingerprint), so growth can land a thought / re-open a miss without a
// per-frame scan. The fingerprint lives in GameState and is SEEDED at world-enter
// from the restored stats: only genuine in-walk growth runs the engine -- never a
// resume, and never a leftover baseline from another pilgrim's walk.
void pumpStatChangeThoughts(GameState& gs)
{
    const int statSum = growth::levelSum(gs.growth);
    if (statSum == gs.stats_seen_sum)
        return;
    gs.stats_seen_sum = statSum;
    const psyche::ObserveResult r = psyche::evaluateStats(gs.psyche, gs.growth, observeNudge);
    applyGains(gs, r); // a stat rising can land a thought -- banked, grown, written down
}

// Teach at the moment the thing is ON SCREEN: edge-detect the box's surfaces, one
// site for every path that can raise them (the deliberate verb, a scene, an Enter
// trigger). Order within a call is pedagogy -- the surface first, its reward
// after -- and fire() dedups by the seen set, so re-checks are free.
// The line kinds' teaching names -- each surfaces as its own first-time event,
// because each IS its own thing (chosen / pressed / spoken / concluded).
const char* lineEventName(psyche::LineKind kind)
{
    switch (kind)
    {
    case psyche::LineKind::Observation:
        return "observation";
    case psyche::LineKind::Impression:
        return "impression";
    case psyche::LineKind::Remark:
        return "remark";
    case psyche::LineKind::Thought:
        return "thought";
    }
    return "";
}

void pumpTutorial(GameState& gs)
{
    auto& tut = gs.tutorial_state;
    // Only a FULLY shown surface teaches: the typewriter has finished (or the
    // menu is open and holding), so the card stops a complete moment -- the
    // player reads the whole thing lit under the hole, never a half-typed line.
    const bool shown = thought_box::fullyShown();
    const bool menuUp = shown && thought_box::menuActive();
    psyche::LineKind kind{};
    const bool lineUp = shown && thought_box::activeLineKind(kind);
    if (lineUp && !tut.line_was_up)
    {
        tutorial::fire(tut, gs.tutorial_config, lineEventName(kind));
        if (thought_box::activeLineEarnedSpirit())
            tutorial::fire(tut, gs.tutorial_config, "spirit");
    }
    if (menuUp && !tut.menu_was_up)
        tutorial::fire(tut, gs.tutorial_config, "deed_menu");
    tut.line_was_up = lineUp;
    tut.menu_was_up = menuUp;
}

// Conclusions are just deeper observations -- they surface the same way.
void pumpUnlockNotifications(GameState& gs)
{
    const std::unordered_set<std::string> now = psyche::availableUnlocks(gs.psyche, gs.growth);
    // A spot's deeds reachable the moment it FIRST becomes observed are baseline --
    // the menu already shows them -- so they seed as announced, silently; only a
    // deed that unlocks AFTER that (a flag lands, a stat rises) is news. Deeper
    // TIERS stay un-seeded on purpose: "more to read here" is real news even at
    // first observation. Seeding lives HERE, not at each observe call site, so
    // every path an observation lands through (deliberate, scene, Enter trigger)
    // obeys the rule -- including a resumed walk, whose whole record seeds on the
    // first pump instead of toasting at boot.
    for (const auto& [spot, tier] : gs.psyche.observed_tier)
        if (gs.seeded_spots.insert(spot).second)
            for (const auto& id : now)
                if (id.find('@') == std::string::npos && id.rfind(spot + ":", 0) == 0)
                    gs.announced_unlocks.insert(id);
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
// The cursor's world position: window pixel -> internal-res pixel (undo the pixel-target's
// aspect-fit blit) -> world (camera-centered, zoom 1). Returns false if there's no active
// camera yet (nothing to anchor to). See ScreenToWorld.
bool mouseWorld(const Engine& engine, const EntityManager& em, float& out_x, float& out_y)
{
    entt::entity cam = entt::null;
    for (auto [e, c] : em.registry().view<Camera>().each())
        if (c.active)
            cam = e;
    if (cam == entt::null)
        return false;
    const auto& c = em.registry().get<Camera>(cam);
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const engine::gl::BlitRect b = engine::gl::computeBlitRect(
        kInternalWidth, kInternalHeight, engine.windowWidth(), engine.windowHeight());
    const auto r =
        screen_to_world::map(static_cast<float>(mx), static_cast<float>(my), b.x, b.y, b.width,
                             b.height, kInternalWidth, kInternalHeight, c.x, c.y);
    out_x = r.x;
    out_y = r.y;
    return true;
}

// Handle the MODAL reading/menu states, which capture input when up. Returns true if a
// modal consumed this frame's input (so the world interaction below is skipped). Click =
// Space everywhere: a click anywhere advances a reading; the action menu is confirmed by
// clicking a hovered option (menuMouse, at the render layer) OR Space. The deliberate
// observe verb itself lives in the InteractionSystem. `clicked` is a consumed left-press
// this frame (so it won't also fire an interactable / leak into the pause page).
bool handleReadingInput(EntityManager& em, GameState& gs, bool clicked)
{
    // The teaching card holds everything -- it stopped the world to point at one
    // thing. Space (or a click) moves past it; nothing else gets this frame.
    if (tutorial::current(gs.tutorial_state) != nullptr)
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE) || clicked)
            tutorial::dismiss(gs.tutorial_state);
        return true;
    }
    if (thought_box::menuActive())
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
            // A deed can fire a thought (EXP) and/or grant an item -- enact both.
            enactConfirm(em, gs, thought_box::confirm(gs.psyche, gs.growth, observeNudge));
        if (pressedThisFrame(em, SDL_SCANCODE_W))
            thought_box::moveUp();
        if (pressedThisFrame(em, SDL_SCANCODE_S))
            thought_box::moveDown();
        if (pressedThisFrame(em, SDL_SCANCODE_F))
            thought_box::back();
        return true;
    }
    if (thought_box::active()) // a reading Line: Space OR a click anywhere advances it
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE) || clicked)
            enactConfirm(em, gs, thought_box::confirm(gs.psyche, gs.growth, observeNudge));
        return true;
    }
    return false;
}

// Toast each deposited item in its rarity color (one line per item so a rare find pops in
// its own color). Stacked quantities show a count.
void toastFinds(const GameState& gs, const std::vector<inventory::ItemInstance>& items)
{
    for (const auto& inst : items)
    {
        const inventory::ItemDef* def = gs.items.find(inst.id);
        if (!def)
            continue;
        // Always show the amount (even 1x) so the format is consistent -- "2x Wild Thyme",
        // "1x Worn River Stone".
        const std::string line = std::to_string(inst.quantity) + "x " + def->name;
        notify::push(line, reading_color::rarityColor(def->rarity));
    }
}

// Deposit item ids + rolled loot tables into the satchel, toasting each find. The ONE place
// an item enters the bag -- shared by the direct-pickup path (interactable action) and the
// deed path (a "pick up"/"gather" deed). `items` are item ids (one each); `tables` are loot
// table ids to roll.
void grantAndToast(GameState& gs, const std::vector<std::string>& items,
                   const std::vector<std::string>& tables)
{
    std::vector<inventory::ItemInstance> deposited;
    for (const auto& id : items)
    {
        inventory::add(gs.satchel, gs.items, inventory::ItemInstance{id});
        deposited.push_back(inventory::ItemInstance{id});
    }
    for (const auto& tableId : tables)
        if (const loot::Table* table = gs.loot_tables.find(tableId))
            for (auto& inst : loot::roll(*table, observeNudge))
            {
                inventory::add(gs.satchel, gs.items, inst);
                deposited.push_back(inst);
            }
    toastFinds(gs, deposited);
    if (!deposited.empty())
        markProgress(gs); // something was found -- worth keeping
}

// Learn a recipe -- the ONE place a recipe becomes known, whether TAUGHT by a deed or discovered
// by a first successful craft. Marks it known and, if it wasn't already, pays the discovery reward
// (a reading-faculty deepening + Spirit EXP, config-driven) and announces it. Idempotent: teaching
// an already-known recipe is a silent no-op, so a re-read rock or a re-craft doesn't re-reward.
// Returns true if this call was the moment it became known.
bool learnRecipe(GameState& gs, const std::string& recipeId)
{
    if (!gs.crafting_state.known.insert(recipeId).second)
        return false; // already known -- no repeat reward
    const crafting::Config& cc = gs.crafting_config;
    grantStatExp(gs, cc.learn_faculty, cc.learn_faculty_exp);
    gs.growth.spirit_exp += cc.learn_spirit_exp;
    notify::push("New recipe learned", kUnlockColor);
    return true;
}

// Run a craft attempt from the Craft tab's selected materials: match them against the recipe
// table (gated by the same knowledge psyche uses), and on an exact match make it -- the
// craft consumes inputs + grants the output, and here we bank its XP, set any first-craft
// reveal flag, and toast the find. Every outcome (made / near-miss / short on materials) surfaces
// as a notification toast; the pot is cleared either way.
void attemptCraft(GameState& gs)
{
    // The pot's distinct item TYPES (match is type-based; the counts staged in the pot are the
    // player's staging, while craft() consumes each recipe's own declared quantity). No knowledge
    // gate -- the ingredients alone decide whether something forms.
    std::vector<std::string> selected;
    selected.reserve(gs.pause.craft_selected.size());
    for (const auto& [id, count] : gs.pause.craft_selected)
        selected.push_back(id);
    const crafting::Match m = crafting::match(selected, gs.recipes);

    if (m.recipe == nullptr)
    {
        // No recipe. A near-miss (some ingredients of a real, realized recipe) nudges; nothing
        // in common stays silent about what's missing (you're on your own -- see CRAFTING.md).
        const bool nearMiss = m.nearest != nullptr && m.closeness >= 0.5f;
        notify::push(nearMiss ? "Something almost forms..." : "These don't belong together.",
                     kCraftMissColor);
        gs.pause.craft_selected.clear();
        return;
    }

    const int craftStat = growth::statLevel(gs.growth, m.recipe->scaling.stat);
    const crafting::Outcome out =
        crafting::craft(*m.recipe, gs.satchel, gs.items, craftStat, observeNudge,
                        gs.crafting_config, gs.crafting_state);
    if (!out.made)
    {
        notify::push("Not enough to work with.", kCraftMissColor);
        gs.pause.craft_selected.clear();
        return;
    }
    // Bank the craft XP into the stat the recipe attributes it to, fire any first-craft reveal,
    // toast the made item. Doing exercises the doing-layer stat exactly as observing exercises a
    // faculty -- the same hook, so a craft toasts and curves like everything else.
    for (const auto& [stat, exp] : out.stat_gains)
        grantStatExp(gs, stat, exp);
    if (!out.revealed_flag.empty())
        gs.psyche.flags.insert(out.revealed_flag);
    if (const inventory::ItemDef* def = gs.items.find(out.output_item))
        notify::push("Made " + std::to_string(out.output_qty) + "x " + def->name,
                     reading_color::rarityColor(def->rarity));

    // Discovering a recipe by making it (never known before) learns it -- the same reward path a
    // taught recipe takes. (No-op if a deed already taught it; then this craft is just a re-make.)
    if (out.first_time)
        learnRecipe(gs, m.recipe->id);
    gs.pause.craft_selected.clear();
    markProgress(gs); // something was made -- worth keeping
}

// The ONE place a pause-page action is enacted. The page produces actions from two input paths
// -- keyboard (step) and mouse (render) -- and both funnel through here, so a new Action variant
// is wired in exactly one spot and neither path can silently drop it. (This is the root fix for
// mouse-Combine doing nothing: render's Craft return was previously handled nowhere.)
void enactPageAction(Engine& engine, EntityManager& em, GameState& gs, pause_page::Action action)
{
    switch (action)
    {
    case pause_page::Action::Quit:
        // No save here: leaving the game is ONE act with ONE write, and it happens on the
        // way out (see main). Saving here too would mean quitting-by-menu and
        // quitting-by-window-close take different paths to the same thing -- two writes to
        // keep in agreement, which is one more than can be kept.
        engine.requestQuit();
        break;
    case pause_page::Action::Leave:
        writeSave(em, gs); // the walk is kept before the world it happened in goes away
        leaveToTitle(em, gs);
        break;
    case pause_page::Action::Settings:
        // The page stays OPEN behind it: settings was reached from the pause page, so
        // backing out of settings returns to the page it was opened from, not to a world
        // that silently unpaused underneath.
        settings_screen::reset();
        gs.app.settings_return_to = app::Phase::Playing;
        gs.app.phase = app::Phase::Settings;
        break;
    case pause_page::Action::Craft:
        attemptCraft(gs);
        break;
    case pause_page::Action::Resume:
        writeSave(em, gs); // closing the page is a natural beat to keep
        break;
    case pause_page::Action::None:
        break;
    }
}

// Despawn the world entity for an encounter id (its glimmer + interactable), e.g. when a
// consumes_spot take removes the whole thing, and remember that it's gone -- otherwise the
// next visit rebuilds the spot from the authored placements and it's back. No-op if the
// spot has no world entity.
void despawnEncounterEntity(EntityManager& em, GameState& gs, const std::string& encounterId)
{
    auto& reg = em.registry();
    for (auto [e, inter] : reg.view<interaction::Interactable>().each())
        if (inter.observe_id == encounterId)
        {
            if (!inter.placement_id.empty())
                gs.gone.insert(inter.placement_id);
            reg.destroy(e);
            return;
        }
}

// Bank the EXP a deed earned + grant any items/tables it declared, and despawn the spot if
// the take consumed it. The chokepoint for a menu confirm's effects (Space or click), so
// both input paths enact them identically.
void enactConfirm(EntityManager& em, GameState& gs, const thought_box::ConfirmResult& r)
{
    applyGains(gs, r);
    if (!r.granted.empty() || !r.gathered.empty())
        grantAndToast(gs, r.granted, r.gathered);
    for (const auto& recipeId : r.taught) // a deed handed over a recipe -> learn it (+ reward)
        learnRecipe(gs, recipeId);
    if (!r.consumed_spot.empty())
        despawnEncounterEntity(em, gs, r.consumed_spot);
    markProgress(gs); // a deed was taken -- worth keeping
}

// Follow-up after the InteractionSystem fired. A direct actionable-only item already deposited
// its find -> toast it. For an encounter spot, the STANCE is the verb: WALKING observes (the
// reading only), RUNNING acts (the deed menu only). The stance badge teaches this; there is no
// per-spot prompt and no reading->menu handoff.
void onInteractionFired(GameState& gs, const interaction::Outcome& out)
{
    if (out.observe_target.empty())
    {
        toastFinds(gs, out.items); // direct pickup/gather -- items already in the satchel
        // A thing taken is gone for good: remember it against the map, or the next visit
        // rebuilds it from the authored placements and it can be taken again.
        if (!out.removed_placement.empty())
            gs.gone.insert(out.removed_placement);
        if (!out.items.empty())
            markProgress(gs); // a find -- worth keeping
        return;
    }
    const std::string& spot = out.observe_target;
    if (out.act)
    {
        // Running (Act stance) -> the deed menu.
        thought_box::openDeedMenu(gs.psyche, gs.growth, spot);
        return;
    }
    // Walking (Observe stance) -> the reading only. Bank its EXP. The unlock pump
    // seeds the spot's baseline deeds as announced (no "new action" toast for what
    // the menu already shows); only later-unlocked deeds announce.
    const psyche::ObserveResult observed =
        thought_box::pushObserve(gs.psyche, gs.growth, spot, observeNudge);
    applyGains(gs, observed);
    markProgress(gs); // a reading landed -- worth keeping
}

// --- scenes: choreography that plays the graph (see Scene.h) -------------------

const ldtk::SpawnPoint* sceneMarker(const GameState& gs, const std::string& id)
{
    for (const auto& s : gs.region_spawns)
        if (s.id == id)
            return &s;
    return nullptr;
}

// Start the first eligible scene: right level, completion flag unset, start_when
// held against current knowledge.
void maybeStartScene(GameState& gs)
{
    for (const auto& d : gs.scenes.scenes)
    {
        if (d.level != gs.region || gs.psyche.flags.count(d.set_flag) > 0)
            continue;
        if (!psyche::conditionMet(gs.psyche, gs.growth, d.start_when))
            continue;
        gs.scene_rt = {};
        gs.scene_rt.active = true;
        gs.scene_rt.def = &d;
        std::fprintf(stderr, "[scene] '%s' begins\n", d.id.c_str());
        return;
    }
}

// A scene's end: raise its completion flag through the engine (later content
// sequences off it), note the progress, release the world.
void finishScene(GameState& gs)
{
    applyGains(gs, psyche::setFlag(gs.psyche, gs.growth, gs.scene_rt.def->set_flag, observeNudge));
    markProgress(gs);
    std::fprintf(stderr, "[scene] '%s' done\n", gs.scene_rt.def->id.c_str());
    gs.scene_rt = {};
}

// The body a scene step steers: one it spawned itself (`enter`), else the level's
// PLACED character of that id -- a scene in the kitchen walks the Mom who is
// already there without re-entering her.
entt::entity sceneBody(const GameState& gs, const std::string& who)
{
    const auto it = gs.scene_rt.bodies.find(who);
    if (it != gs.scene_rt.bodies.end())
        return it->second;
    const auto placed = gs.region_npcs.find(who);
    return placed != gs.region_npcs.end() ? placed->second : entt::null;
}

void sceneFace(EntityManager& em, entt::entity body, float dx, float dy)
{
    if (auto* f = em.registry().try_get<FacingDirection>(body))
    {
        f->dx = dx;
        f->dy = dy;
        f->render_dx = dx;
        f->render_dy = dy;
    }
}

void sceneAnim(EntityManager& em, entt::entity body, int row, int frames, float duration)
{
    auto* a = em.registry().try_get<Animation>(body);
    if (a != nullptr && a->current_row != row)
    {
        a->current_row = row;
        a->current_frames = frames;
        a->current_duration = duration;
    }
}

// An npc appears at a marker. Instant.
bool sceneEnterStep(EntityManager& em, GameState& gs, const scene::Step& s)
{
    const auto* m = sceneMarker(gs, s.target);
    const auto cfg = gs.npcs.npcs.find(s.who);
    if (m == nullptr || cfg == gs.npcs.npcs.end())
    {
        std::fprintf(stderr, "[scene] enter '%s' at '%s': unknown npc/marker -- skipped\n",
                     s.who.c_str(), s.target.c_str());
        return true;
    }
    gs.scene_rt.bodies[s.who] = npc::spawn(em, cfg->second, m->wx, m->wy, s.facing);
    return true;
}

// Walk the body toward the marker; true on arrival. Straight line, no
// pathfinding -- the author keeps the path clear.
bool sceneMoveStep(EntityManager& em, GameState& gs, const scene::Step& s, float dt)
{
    const entt::entity body = sceneBody(gs, s.who);
    const auto* m = sceneMarker(gs, s.target);
    const auto cfg = gs.npcs.npcs.find(s.who);
    if (body == entt::null || m == nullptr || cfg == gs.npcs.npcs.end() ||
        !em.registry().valid(body))
    {
        std::fprintf(stderr, "[scene] move '%s' -> '%s': missing body/marker -- skipped\n",
                     s.who.c_str(), s.target.c_str());
        return true;
    }
    const npc::Config& c = cfg->second;
    auto& t = em.registry().get<Transform>(body);
    const float dx = m->wx - t.x;
    const float dy = m->wy - t.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float step = c.walk_speed * dt;
    if (dist <= step)
    {
        t.x = m->wx;
        t.y = m->wy;
        sceneAnim(em, body, c.idle_row, c.idle_frames, c.idle_duration);
        return true;
    }
    t.x += dx / dist * step;
    t.y += dy / dist * step;
    sceneFace(em, body, dx / dist, dy / dist);
    sceneAnim(em, body, c.walk_row, c.walk_frames, c.walk_duration);
    return false;
}

// Fire encounter content through the box -- the step where a scene hands the pace
// to the player. A blocking step waits for the box to clear; a non-blocking observe
// fires and moves on (the body keeps walking while its words are read); a
// non-blocking menu holds only until the CHOICE lands, so the reply text overlaps
// whatever the scene does next.
bool sceneBoxStep(GameState& gs, const scene::Step& s, bool menu)
{
    auto& rt = gs.scene_rt;
    if (!rt.pushed)
    {
        if (!menu)
        {
            // A scene firing content at the player is the world reaching him
            // unbidden -- an observe step is an impression, a remark step is a
            // voice made to speak now.
            if (s.kind == scene::Step::Kind::Remark)
                applyGains(gs, psyche::forceRemark(gs.psyche, gs.growth, s.target, observeNudge));
            else
                applyGains(gs, thought_box::pushObserve(gs.psyche, gs.growth, s.target,
                                                        observeNudge, /*impression=*/true));
            if (!s.blocking)
                return true;
            rt.pushed = true;
            return false;
        }
        // A menu must wait its turn: earlier readings (a non-blocking observe's
        // lines) are still on screen or queued -- the choice comes after the words.
        if (!thought_box::settled() || !gs.psyche.pending.empty())
            return false;
        thought_box::openDeedMenu(gs.psyche, gs.growth, s.target, s.must_choose);
        rt.pushed = true;
        return false;
    }
    // Blocking steps wait for the WHOLE exchange (settled + nothing queued to
    // surface); a non-blocking menu only until the choice lands.
    const bool busy = menu && !s.blocking ? thought_box::menuActive()
                                          : (!thought_box::settled() || !gs.psyche.pending.empty());
    if (busy)
        return false;
    rt.pushed = false;
    return true;
}

void sceneLeaveStep(EntityManager& em, GameState& gs, const scene::Step& s)
{
    const entt::entity body = sceneBody(gs, s.who);
    if (body != entt::null && em.registry().valid(body))
        em.registry().destroy(body);
    gs.scene_rt.bodies.erase(s.who);
}

void sceneFaceStep(EntityManager& em, const GameState& gs, const scene::Step& s)
{
    float dx = 0.0f;
    float dy = 1.0f;
    ldtk::facingVec(s.facing, dx, dy);
    const entt::entity body = sceneBody(gs, s.who);
    if (body != entt::null && em.registry().valid(body))
        sceneFace(em, body, dx, dy);
}

bool sceneWaitStep(GameState& gs, const scene::Step& s, float dt)
{
    gs.scene_rt.timer += dt;
    if (gs.scene_rt.timer < s.seconds)
        return false;
    gs.scene_rt.timer = 0.0f;
    return true;
}

// How dark a scene's FadeIn holds the screen right now: 1 (full black) -> 0 as the
// step's timer runs. Non-zero only while a FadeIn step IS the current step -- the
// scene starts (and first renders) at full black, so an opening literally wakes up.
float sceneFadeAlpha(const GameState& gs)
{
    const auto& rt = gs.scene_rt;
    if (!rt.active || rt.step >= rt.def->steps.size())
        return 0.0f;
    const scene::Step& s = rt.def->steps[rt.step];
    if (s.kind != scene::Step::Kind::FadeIn || s.seconds <= 0.0f)
        return 0.0f;
    return 1.0f - std::clamp(rt.timer / s.seconds, 0.0f, 1.0f);
}

// One step; true = complete (advance to the next).
bool runSceneStep(EntityManager& em, GameState& gs, const scene::Step& s, float dt)
{
    switch (s.kind)
    {
    case scene::Step::Kind::Enter:
        return sceneEnterStep(em, gs, s);
    case scene::Step::Kind::Leave:
        sceneLeaveStep(em, gs, s);
        return true;
    case scene::Step::Kind::Move:
        return sceneMoveStep(em, gs, s, dt);
    case scene::Step::Kind::Face:
        sceneFaceStep(em, gs, s);
        return true;
    case scene::Step::Kind::Wait:
    case scene::Step::Kind::FadeIn: // same clock; the overlay reads the step's progress
        return sceneWaitStep(gs, s, dt);
    case scene::Step::Kind::Observe:
    case scene::Step::Kind::Remark:
        return sceneBoxStep(gs, s, /*menu=*/false);
    case scene::Step::Kind::Menu:
        return sceneBoxStep(gs, s, /*menu=*/true);
    case scene::Step::Kind::SetFlag:
        applyGains(gs, psyche::setFlag(gs.psyche, gs.growth, s.target, observeNudge));
        return true;
    case scene::Step::Kind::Sound:
        ambience::start(gs.ambience_state, gs.ambience_config, s.target);
        return true;
    case scene::Step::Kind::StopSound:
        ambience::stop(gs.ambience_state, gs.ambience_config, s.target);
        return true;
    }
    return true;
}

// The gated score enters the moment its flag lands (enterWorld handles walks that
// already hold it, and worlds with no gate).
void tickMusicGate(GameState& gs)
{
    if (gs.music_started || gs.psyche.flags.count(gs.world_config.ambient_gate_flag) == 0)
        return;
    gs.music_started = true;
    AudioSystem::playMusic(gs.world_config.ambient_track, gs.world_config.ambient_volume,
                           /*loop=*/true, gs.world_config.ambient_fade_in_ms);
}

// WHO HAS THE FLOOR this tick -- the one statement of the input policy, computed
// once and asked semantic questions, instead of each consumer or-ing its own
// subset of modal states. New authorities (a cutscene camera, a sleep fade) add a
// field + amend the answers HERE, not another conditional at every call site.
struct Control
{
    bool menu = false;  // the action menu is modal (its W/S drive selection)
    bool fade = false;  // a warp fade runs (the crossing step is committed)
    bool scene = false; // a scene runs (the world has the floor)
    bool card = false;  // a teaching card is up (the world holds its breath)

    // Movement keys: frozen by any authority above (the box still takes its own
    // input -- readings and menus keep their pace even mid-scene).
    bool movementFrozen() const
    {
        return menu || fade || scene || card;
    }
    // Starting NEW world interactions (observe/act on what's in reach): only the
    // player's to do, and only when the world isn't speaking.
    bool mayInteract() const
    {
        return !scene && !card;
    }
};

Control control(const GameState& gs)
{
    Control c;
    c.menu = thought_box::menuActive();
    c.fade = gs.warp_fade.phase != GameState::WarpFade::Phase::None;
    c.scene = gs.scene_rt.active;
    c.card = tutorial::current(gs.tutorial_state) != nullptr;
    return c;
}

// The per-tick bookkeeping running under the world (after the pause guard --
// a paused world holds all of this still):
//   - world time; a teaching card holds it too (the moment is stopped, not
//     elapsing), which also drives notebook datelines and the later day/night;
//   - the autosave throttle (writes when something worth keeping landed);
//   - the ambience flag bus (a stop_on_flag channel dies the moment its flag
//     exists -- the TV goes quiet because the deed turned it off);
//   - the gated score.
void tickWorldBookkeeping(EntityManager& em, GameState& gs, const Control& ctl, double dt)
{
    if (!ctl.card)
        worldclock::tick(gs.clock, dt);
    autosaveTick(em, gs, dt);
    ambience::tick(gs.ambience_state, gs.ambience_config, gs.psyche.flags);
    tickMusicGate(gs);
}

// Tick: start an eligible scene when idle; else advance the running one. Player
// movement holds while active (gameUpdate); the box keeps taking input, which is
// how Observe/Menu steps let the player set the reading pace. Instant steps chain
// within one tick; blocking ones return and resume next tick.
void tickScene(EntityManager& em, GameState& gs, float dt)
{
    if (!gs.scene_rt.active)
    {
        maybeStartScene(gs);
        if (!gs.scene_rt.active)
            return;
    }
    while (gs.scene_rt.step < gs.scene_rt.def->steps.size())
    {
        if (!runSceneStep(em, gs, gs.scene_rt.def->steps[gs.scene_rt.step], dt))
            return;
        ++gs.scene_rt.step;
    }
    finishScene(gs);
}
} // namespace

void setWorldEnter(WorldEnterFn fn)
{
    sWorldEnter = fn;
}

void setRegionSwitch(RegionSwitchFn fn)
{
    sRegionSwitch = fn;
}

void saveNow(const EntityManager& em, GameState& gs)
{
    writeSave(em, gs);
}

void clampCameraToMap(EntityManager& em, const GameState& gs)
{
    auto* cam = em.registry().try_get<Camera>(gs.player);
    if (cam == nullptr)
        return;
    const float mapW = static_cast<float>(em.tile_map.width * em.tile_map.tile_size);
    const float mapH = static_cast<float>(em.tile_map.height * em.tile_map.tile_size);
    const float halfW = static_cast<float>(kInternalWidth) * 0.5f;
    const float halfH = static_cast<float>(kInternalHeight) * 0.5f;
    cam->x = mapW <= halfW * 2.0f ? mapW * 0.5f : std::clamp(cam->x, halfW, mapW - halfW);
    cam->y = mapH <= halfH * 2.0f ? mapH * 0.5f : std::clamp(cam->y, halfH, mapH - halfH);
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    auto& reg = em.registry();
    GameState& gs = reg.ctx().get<GameState>();

    // A menu is not the world: no world exists yet to tick (the region is built when the
    // player commits -- see walkAs). Everything below assumes a built world.
    if (inMenu(gs))
    {
        updateMenu(engine, em, gs, dt);
        return;
    }

    tickWarpFade(engine, em, gs, dt);

    handleWorldHotkeys(em);

    // A stat change re-runs the engine over the stat keys (growth can land a
    // thought or re-open a prior miss). Then announce any newly-reachable
    // observations/actions (the pull-back to a spot).
    pumpStatChangeThoughts(gs);

    // Cheap + idempotent (diffs against announced_unlocks), so once/frame.
    pumpUnlockNotifications(gs);

    // First-time teaching cards, fired off what is on screen right now.
    pumpTutorial(gs);

    // Snapshot positions for render interpolation before anything moves a body
    // this tick -- the player's integration OR a scene's choreography.
    for (auto [e, t, pt] : reg.view<Transform, PreviousTransform>().each())
    {
        pt.x = t.x;
        pt.y = t.y;
    }

    // Scenes: the world acting on its own. Held while the pause page freezes the
    // world, like everything else in it -- and while a teaching card has stopped
    // the moment to point at it.
    if (!gs.pause.open && tutorial::current(gs.tutorial_state) == nullptr)
        tickScene(em, gs, static_cast<float>(dt));

    // Who has the floor this tick (see Control). AFTER the scene tick, so a scene
    // that just started (or just opened a menu) governs input this very tick.
    const Control ctl = control(gs);

    enactPageAction(engine, em, gs, stepPausePage(em, gs, ctl.menu));

    // Freeze the world while the page is open: skip movement, observing, camera.
    // Animation is halted in gamePreRender (it runs at wall-clock rate there).
    if (gs.pause.open)
        return;

    tickWorldBookkeeping(em, gs, ctl, dt);

    const PlayerConfig& pc = gs.player_config;

    // Movement keys go through the ONE policy (see Control). Readings leave you
    // free to walk -- and walking away dismisses them (below).
    static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {};
    const Uint8* keys = ctl.movementFrozen() ? kNoKeys : SDL_GetKeyboardState(nullptr);

    // Hold Shift to fast-walk: faster movement + brisker leg cadence.
    const bool fast = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
    const float speed = fast ? pc.speed * pc.run_speed_mult : pc.speed;

    // Interaction stance = running vs walking (raw Shift, so it tracks the true stance even
    // with a menu up, where movement is frozen). Plays the enter-Act / enter-Observe SFX on a
    // transition; the badge (rendered in the UI pass) shows the current stance.
    const Uint8* rawKeys = SDL_GetKeyboardState(nullptr);
    const bool running = rawKeys[SDL_SCANCODE_LSHIFT] != 0 || rawKeys[SDL_SCANCODE_RSHIFT] != 0;
    interaction_mode::update(gs.int_mode_state, gs.int_mode_config, running);
    const player_movement::MoveIntent intent = player_movement::update(
        em, gs.player, keys, speed, pc.corner_nudge, pc.corner_slide, static_cast<float>(dt));

    // Facing + state from the move INTENT, not the resulting velocity. Pressed against a wall,
    // collision zeroes velocity but the player is still trying to walk -- so the walk cycle
    // keeps playing (velocity-driven, it would freeze to idle against a tree). Intent is the
    //
    // Facing follows INTENT (constant while a key is held), so a corner where collision
    // oscillates the velocity can't flicker the sprite. AnimationSystem snaps it to a cardinal
    // via snapFacing's hysteresis. Left at its last value when idle, so you keep facing where
    // you were headed.
    auto& anim = reg.get<Animation>(gs.player);
    if (intent.moving())
    {
        auto& facing = reg.get<FacingDirection>(gs.player);
        facing.render_dx = intent.dx;
        facing.render_dy = intent.dy;
    }
    updatePlayerAnim(anim, intent.moving(), pc, fast);

    // Footstep SFX: a footfall matching the surface under the player, on a speed-scaled
    // cadence while moving. Driven by intent (like the walk cycle), so pressing into a wall
    // still steps -- the legs are moving even when collision holds you in place.
    const bool moving = intent.moving();
    const auto& ptf = reg.get<Transform>(gs.player);
    const std::string surface = surfaceUnder(em, gs, ptf.x, ptf.y);
    footsteps::update(gs.footstep_state, gs.footstep_config, surface, moving, fast,
                      static_cast<float>(dt));

    // Player position -- observation glow + interaction are proximity-based (no facing).
    const auto& pt = reg.get<Transform>(gs.player);

    detectWarpCrossing(em, gs, pt.x, pt.y, intent.dx, intent.dy, speed * static_cast<float>(dt));

    // Ambient triggers: Enter encounters (areas, moods) fire on their own when the player
    // is within range -- no observe verb. Deliberate object observing stays in
    // handleObserveInput. Fires once each; earns Spirit EXP like a deliberate reading, and
    // its thoughts are written down like one.
    const psyche::ObserveResult ambient =
        psyche::triggerProximity(gs.psyche, gs.growth, pt.x, pt.y, observeNudge);
    applyGains(gs, ambient);

    // Read the left-click ONCE (mouseClicked consumes it) so the same click can't both
    // advance a reading AND fire an interactable / leak to the pause page.
    const bool clicked = clickedThisFrame(em, SDL_BUTTON_LEFT);

    // World interaction: a reading/menu, if up, consumes input (modal -- click = Space,
    // advancing it). Otherwise resolve the active interactable (nearest in reach or under
    // the cursor), and Space OR a click ON it fires -- observe now, pick-up/craft/open
    // later. The InteractionSystem also sets the `active` flag that drives the glow.
    // During a scene the box still takes input (its readings/menus ARE the scene's
    // pace), but no NEW world interaction can start -- the world has the floor.
    if (!handleReadingInput(em, gs, clicked) && ctl.mayInteract())
    {
        interaction::Intent intent;
        intent.px = pt.x;
        intent.py = pt.y;
        intent.mouse_valid = mouseWorld(engine, em, intent.mouse_x, intent.mouse_y);
        intent.pressed = pressedThisFrame(em, SDL_SCANCODE_SPACE);
        intent.clicked = clicked;
        // Running (Shift held) makes interacting an ACT (skip to the deed menu); walking
        // observes. Slow down to notice; move with intent to do.
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);
        intent.act = keyState[SDL_SCANCODE_LSHIFT] != 0 || keyState[SDL_SCANCODE_RSHIFT] != 0;
        const interaction::Context ctx{gs.psyche,
                                       gs.growth,
                                       observeNudge,
                                       gs.satchel,
                                       gs.items,
                                       gs.loot_tables,
                                       gs.psyche.interact_reach};
        // Decide which encounters are present THIS frame before resolving a target, so a
        // hidden one is never interactable and a just-revealed one is.
        glimmer::refreshPresence(em, gs.psyche, gs.growth);
        const interaction::Outcome fired = interaction::update(em, intent, ctx);
        gs.growth.spirit_exp += fired.earned;
        if (fired.fired)
            onInteractionFired(gs, fired);
    }

    // World glimmer: an encounter glows only while it's the active target; its brightness is
    // the Perception formula (same for every spot). Fades to 0 otherwise -- nothing lingers.
    // Runs after the InteractionSystem set `active`.
    glimmer::update(em, gs.growth, gs.formulas, gs.glimmer_config, static_cast<float>(dt));

    // World items: the active one gets a lit rim outline (its own edge lights -- the pickup
    // cue, distinct from the observation glimmer). Also runs after `active` is set.
    world_items::updateOutlines(em, gs.world_items_config);

    // Over-head thought bubble: shown exactly while a thought reading is on screen,
    // then fades. It tracks the player's head each frame.
    Color thoughtHue{};
    const bool thoughtUp = thought_box::activeThought(gs.growth, thoughtHue);
    head_marker::set(em, thoughtUp);
    head_marker::update(em, gs.head_marker_config, pt.x, pt.y, static_cast<float>(dt));

    // Snap the active camera to its entity (the player), then keep the view on the map.
    CameraSystem::update(em);
    clampCameraToMap(em, gs);
}

void gamePreRender(Engine& engine, EntityManager& em)
{
    auto& gs = em.registry().ctx().get<GameState>();

    // Nothing to animate or drain before there is a world.
    if (!gs.app.world_built)
        return;

    // The page freezes the world: hold the sprite pose and stop draining
    // monologue lines (both advance at wall-clock rate, so they must be gated
    // here rather than in the fixed-step update).
    if (gs.pause.open)
        return;

    // Sprite-sheet animation advances at wall-clock frame rate, not the fixed
    // tick (see engines/engine/docs/ENGINE.md "Animation system").
    AnimationSystem::update(em, static_cast<float>(engine.frameDt()));

    // A teaching card stops the moment: the typewriter (and any queued line)
    // holds under it, exactly where it was, until the card is read.
    if (tutorial::current(gs.tutorial_state) == nullptr)
        thought_box::update(gs.psyche, gs.growth, static_cast<float>(engine.frameDt()),
                            engine.windowWidth(), engine.windowHeight());
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)alpha;

    // The target still opens + blits with no world (it clears to the ambient color, which
    // is what the title sits on); only the world passes are skipped -- there is nothing to
    // draw until the player commits.
    engine::gl::pixelTargetBegin(kAmbientR, kAmbientG, kAmbientB);
    if (em.registry().ctx().get<GameState>().app.world_built)
    {
        // Render at the internal resolution (the pixel target's viewport), zoom 1.
        // camX/camY are the engine-interpolated active-camera position. Draw order:
        // GROUND -> DECORATION (flowers, under player) -> character sprites -> OVERHANG
        // (canopy tops, over player = walk-behind). The sparse passes no-op if absent.
        TileMapRenderer::render(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
        TileMapRenderer::renderDecoration(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
        RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
        TileMapRenderer::renderOverhang(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
    }
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());
}

// Draw the warp boxes + the player's body box exactly where the game computes them,
// through the same internal-resolution blit mapping the mouse uses -- so what fires and
// what draws can never disagree. Body box: green = armed, orange = disarmed (latched).
void drawWarpDebug(const Engine& engine, EntityManager& em, const GameState& gs)
{
    entt::entity camE = entt::null;
    for (auto [e, c] : em.registry().view<Camera>().each())
        if (c.active)
            camE = e;
    if (camE == entt::null)
        return;
    const auto& cam = em.registry().get<Camera>(camE);
    const engine::gl::BlitRect b = engine::gl::computeBlitRect(
        kInternalWidth, kInternalHeight, engine.windowWidth(), engine.windowHeight());
    if (b.width <= 0 || b.height <= 0)
        return;
    const float sx = static_cast<float>(b.width) / static_cast<float>(kInternalWidth);
    const float sy = static_cast<float>(b.height) / static_cast<float>(kInternalHeight);
    const auto toScreenX = [&](float wx) {
        return static_cast<float>(b.x) +
               (wx - cam.x + static_cast<float>(kInternalWidth) * 0.5f) * sx;
    };
    const auto toScreenY = [&](float wy)
    {
        return static_cast<float>(b.y) +
               (wy - cam.y + static_cast<float>(kInternalHeight) * 0.5f) * sy;
    };
    const auto box = [&](float cx, float cy, float w, float h, const Color& fill)
    {
        const float x0 = toScreenX(cx - w * 0.5f);
        const float y0 = toScreenY(cy - h * 0.5f);
        UIRenderer::drawRect(x0, y0, w * sx, h * sy, fill);
    };
    for (const auto& w : gs.region_warps)
        box(w.x, w.y, w.w, w.h, Color{0.55f, 0.30f, 0.95f, 0.35f});
    if (em.registry().valid(gs.player))
    {
        const auto& t = em.registry().get<Transform>(gs.player);
        if (const auto* col = em.registry().try_get<Collider>(gs.player))
        {
            const Color c =
                gs.warp_armed ? Color{0.2f, 0.95f, 0.3f, 0.45f} : Color{0.95f, 0.6f, 0.15f, 0.45f};
            box(t.x, t.y, col->width, col->height, c);
        }
    }
}

// The over-everything layer, in its stacking order: the teaching card's hold
// (dim + hone + the card), the notification toasts above it (a card pointing at
// the notification band must leave the toast it points at lit -- and stops its
// clock, dt 0, so it can't fade out under the card), the screen fade above all
// (the warp's quick dip or a scene's slow waking; whichever holds more darkness
// wins).
void renderOverEverything(const Engine& engine, GameState& gs, int ww, int wh)
{
    const tutorial::Card* card = tutorial::current(gs.tutorial_state);
    if (card != nullptr)
        tutorial::render(*card, gs.hud, ww, wh);
    notify::render(card != nullptr ? 0.0f : static_cast<float>(engine.frameDt()), ww, wh);
    if (const float a = std::max(warpFadeAlpha(gs), sceneFadeAlpha(gs)); a > 0.0f)
        UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(ww), static_cast<float>(wh),
                             Color{0.0f, 0.0f, 0.0f, a});
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    auto& gs = em.registry().ctx().get<GameState>();
    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();

    if (!inMenu(gs) && sShowWarpDebug)
        drawWarpDebug(engine, em, gs);

    // The greeting IS the screen -- none of the world's HUD applies, and the mouse belongs
    // to the title. Its action funnels through the same sink the keys use. Note the shared
    // exit through clearOneShotInput below: an early return here would leave this frame's
    // key presses in the buffer to be re-read forever.
    if (inMenu(gs))
    {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        const float fx = static_cast<float>(mx);
        const float fy = static_cast<float>(my);
        const bool clicked = engine::ui::mouseClicked(em, SDL_BUTTON_LEFT);

        switch (gs.app.phase)
        {
        case app::Phase::Greeting:
            enactTitleAction(engine, em, gs,
                             title_screen::render(title_screen::Mouse{fx, fy, clicked},
                                                  (gs.app.pilgrim_count > 0), ww, wh));
            break;
        case app::Phase::Naming:
            enactNameAction(engine, em, gs,
                            name_screen::render(name_screen::Mouse{fx, fy, clicked}, ww, wh));
            break;
        case app::Phase::Loading:
        {
            const savegame::File file = savegame::load();
            enactLoadAction(engine, em, gs,
                            load_screen::render(rosterEntries(file, gs),
                                                load_screen::Mouse{fx, fy, clicked}, ww, wh));
            break;
        }
        case app::Phase::Settings:
            enactSettingsAction(gs, settings_screen::render(
                                        gs.prefs, settings_screen::Mouse{fx, fy, clicked}, ww, wh));
            break;
        case app::Phase::Playing:
            break; // not a menu
        }
        clearOneShotInput(em);
        return;
    }

    // CONTENT -- the game speaking. Not the HUD's business and not settable: the reading
    // box shows because something is being said, and hiding it would mute the game rather
    // than tidy the screen (see "Content is not HUD" in docs/design/HUD.md). Drawn in
    // native window space (the engine's UI pass runs after the world blit, at window
    // resolution); tinted by faculty + rarity via the growth state.
    thought_box::render(gs.growth, ww, wh);

    // HUD -- status, on screen because it is always true. The visibility mode governs the
    // lot of it, and each piece answers to its own setting besides: a piece switched off is
    // gone at any mode. The pause page is separate (F-gated), so Off still opens it.
    if (gs.prefs.hud.visibility != hud::Visibility::Off)
    {
        // The stance badge (Observe / Act), so the player always knows which verb an
        // interact will do.
        if (gs.prefs.hud.show_stance)
            interaction_mode::render(gs.int_mode_state, gs.int_mode_config, ww, wh);

        // What the watch says, while one is carried. The world's clock is frozen behind the
        // pause page, so this reads as a held hand there rather than a stale number.
        watch_hud::render(gs.watch_hud_config, gs.clock,
                          gs.prefs.hud.show_time && inventory::has(gs.satchel, "watch"), ww, wh);
    }

    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const bool lClick = engine::ui::mouseClicked(em, SDL_BUTTON_LEFT);

    // Mouse on the action menu (interchangeable with W/S + Space): hover an option
    // to highlight, click to confirm. Runs after render() stashes the menu's geometry.
    // The pause page can't be open while the menu is up, so the click is theirs to
    // share without conflict. Not gated on the HUD mode: a deed menu is CONTENT -- the
    // player opened it and it is asking them something -- so hiding the HUD must never
    // leave it on screen with dead clicks.
    enactConfirm(em, gs,
                 thought_box::menuMouse(gs.psyche, gs.growth, observeNudge, static_cast<float>(mx),
                                        static_cast<float>(my), lClick));

    // Pause page over everything (no-op when closed). Mouse is interchangeable
    // with the keyboard controls: hover a tab to highlight, click to switch,
    // click Quit on the System tab to exit. Mouse handling lives here because it
    // hit-tests the geometry render() draws.
    const pause_page::Mouse mouse{static_cast<float>(mx), static_cast<float>(my), lClick};
    const pause_page::Content content{gs.psyche, gs.satchel, gs.items,         gs.notebook,
                                      gs.clock,  gs.recipes, gs.crafting_state};
    // Resolve item-icon paths to textures through the engine's cache (the page stays engine-
    // type-free). Empty path -> 0 (a swatch fallback in the grid).
    const pause_page::IconResolver icon = [&engine](const std::string& path) -> std::uint32_t
    { return path.empty() ? 0u : engine.textureManager().load(path); };
    // The mouse action path funnels through the SAME enactPageAction as the keyboard, so a
    // Combine click enacts the craft exactly like Space does (no per-action wiring to forget).
    enactPageAction(engine, em, gs,
                    pause_page::render(gs.pause, gs.growth, content, mouse, icon,
                                       engine.windowWidth(), engine.windowHeight()));

    renderOverEverything(engine, gs, ww, wh);

    clearOneShotInput(em);
}

void gameRenderImGui(Engine& /*engine*/, EntityManager& em)
{
    // Dev tunables panel (F1). No-op when hidden. Edits player_config + stats
    // live; the Cognition tab shows the observation state's live tree.
    auto& gs = em.registry().ctx().get<GameState>();
    tune_panel::render(gs.player_config, gs.growth, gs.psyche);
}
