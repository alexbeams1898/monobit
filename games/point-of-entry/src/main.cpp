#include "Engine.h"
#include "FontManager.h"
#include "UIRenderer.h"
#include "ecs/AppState.h"
#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/FeelConfig.h"
#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "formats/AreaLoader.h"
#include "formats/RoomGen.h"
#include "formats/SpriteDefLoader.h"
#include "gl/PixelRenderTarget.h"
#include "ops/AreaBuildOps.h"
#include "ops/CaptureUtils.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "ops/RecordOps.h"
#include "ops/SaveOps.h"
#include "ops/SoundOps.h"
#include "ops/SpawnUtils.h"
#include "ops/ZoneUtils.h"
#include "renderers/DebugPanelRenderer.h"
#include "renderers/FloaterRenderer.h"
#include "renderers/HudRenderer.h"
#include "renderers/NotificationRenderer.h"
#include "renderers/PromptRenderer.h"
#include "screens/PauseScreen.h"
#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"
#include "screens/SettingsScreen.h"
#include "screens/StagingScreen.h"
#include "screens/TitleScreen.h"
#include "systems/AimSystem.h"
#include "systems/AnimationSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CombatSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DescentSystem.h"
#include "systems/FlowFieldSystem.h"
#include "systems/GaitSystem.h"
#include "systems/PickupSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RenderSystem.h"
#include "systems/RewardSystem.h"
#include "systems/SpriteAnimSystem.h"
#include "systems/ThermosSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/TintSystem.h"
#include "systems/TravelSystem.h"
#include "systems/WaveSystem.h"
#include "utils/CrashHandler.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

#include <glad/glad.h>

// Point of Entry -- the skeleton. Right now it does one thing: build a floor out
// of hand-authored ASCII rooms, put the player in it, and let him walk around.
// Everything else (the gadget, sealing, the swarm) arrives on top of this.

namespace
{
// THE INTERNAL RESOLUTION IS DERIVED, not fixed. Everything renders at window/zoom and the
// whole buffer is then scaled up by that integer -- the pixel-art pipeline.
//
// Derived rather than constant because a FIXED internal size means the same character is a
// different physical size on every display: 1280x720 fills a 1440p monitor at 2x and a laptop
// at rather less, so the art shrinks when you undock. Dividing the window instead keeps one
// drawn pixel equal to `zoom` screen pixels everywhere.
//
// One rounding pass, too. Drawing straight to the window means the camera and every sprite
// round to a pixel independently, and at a zoom those errors do not cancel -- things pop
// against each other whenever frame times jitter, which reads as lag.
int internalW(const Engine& engine)
{
    return engine.windowWidth() / debug_panel::zoom();
}
int internalH(const Engine& engine)
{
    return engine.windowHeight() / debug_panel::zoom();
}

// The shell's state, and the handles the callbacks need. File-scope because the engine's
// callbacks are plain function pointers -- there is no user-data slot to thread them through.
app::State sApp;
Engine* sEngine = nullptr;

// The cellar's unlit dark -- what shows where no tile is drawn.
constexpr float kVoidR = 0.05f;
constexpr float kVoidG = 0.05f;
constexpr float kVoidB = 0.06f;

// Placeholder art: the engine draws a coloured quad for a sprite with no
// texture, which is all this needs until the real pixel art exists.
constexpr float kPlayerSize = 28.0f;
constexpr float kPoeSize = 32.0f;

// THE DEATH BEAT. No drama, no announcement, no question -- the register is a
// man who never cracks, so the world fades to black and he is simply home,
// whole. The game never explains how he got there. The world FREEZES for the
// duration: watching the swarm keep boiling while you fade would read as the
// game continuing without you, which is a different (and wrong) statement.
enum class DeathBeat
{
    None,
    FadeOut,
    FadeIn
};
DeathBeat sDeathBeat = DeathBeat::None;
float sDeathTimer = 0.0f;
constexpr float kDeathFadeOut = 0.7f;
constexpr float kDeathFadeIn = 0.7f;

// The exterminator himself, created once and carried through every world he
// enters. No-op when he already exists -- his sheet and pocket are him.
void ensurePlayer(EntityManager& em, float x, float y)
{
    if (em.registry().valid(player::entity()))
        return;
    const entt::entity playerEnt = spawn::box(em, x, y, kPlayerSize, 0.85f, 0.84f, 0.78f);
    // Real art, if the pipeline has produced any. No def means the placeholder
    // box stands in, loudly (SpriteDef::load logs).
    if (const sprite_def::Def def = sprite_def::load("assets/sprites/player.json"); def.ok)
    {
        auto& spr = em.registry().get<Sprite>(playerEnt);
        spr.texture_path = def.sheet;
        spr.src_w = def.frame_w;
        spr.src_h = def.frame_h;
        em.registry().remove<SolidColor>(playerEnt);
        sprite_anim::attach(em, playerEnt, def);
    }
    // The foot box. Smaller than the sprite and at its bottom, so his torso
    // may overlap walls above him (top-down depth) while his feet stay out.
    em.registry().emplace<Collider>(playerEnt, Collider{16.0f, 12.0f});
    // No authored health: the sheet is the only source, and the numbers derive from it.
    em.registry().emplace<Stats>(playerEnt, stats::playerStart());
    stats::applyDerivations(em, playerEnt);
    // The tank is part of the man, not a side effect of the first shot -- it
    // must read true on the HUD in places where the trigger never fires.
    em.registry().emplace<Charge>(playerEnt,
                                  Charge{tools::chargeTuning().max, tools::chargeTuning().max});
    em.registry().emplace<Camera>(playerEnt, Camera{x, y});
    // He is the one thing in the game with legs, so he is the one thing with a gait -- and
    // with it, footsteps.
    em.registry().emplace<Gait>(playerEnt, Gait{});
    player::bind(playerEnt);
}

void loadConfigs()
{
    feel::load("config/feel.json");
    stats::load("config/stats.json");
    tint::load("config/stats.json");
    thermos::load("config/stats.json");
    items::load("config/items");
    tools::load("config/tools.json");
    sound::load("config/audio.json");
}

// The way down underfoot, if any. Measured from the site's MOUTH (mouth_x/y)
// rather than its art: a wall-mounted hole's art sits in the wall, and the
// standable spot is the floor at its base -- the same point its pests
// surface at, whatever kind of placement put it there.
entt::entity passageUnderfoot(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return entt::null;
    const auto& pt = reg.get<Transform>(p);
    for (const auto [e, site] : reg.view<PlacedHole>().each())
    {
        const float dx = pt.x - site.mouth_x;
        const float dy = pt.y - site.mouth_y;
        if (dx * dx + dy * dy < site.reach * site.reach)
            return e;
    }
    return entt::null;
}

// DEATH. It costs the floor's progress and the walk back down -- nothing
// else. He wakes at home, whole, the pocket with him; the pocket is the POINT
// of dying being cheap: the walk back down is already the tension, and taking
// the money too would punish the same mistake twice.
void deathReturn(Engine& engine, EntityManager& em)
{
    auto& reg = em.registry();

    // The descent is untouched -- death writes nothing on the world. He just
    // stops standing in it.
    const std::string start = area::startLevel("assets/maps/world.ldtk");
    if (!start.empty() && travel::enter(engine, em, start))
        descent::leave();
    else
        poe::log().error("world: no start level to wake in");

    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    if (auto* hp = reg.try_get<Health>(p))
        hp->current = hp->max;
    if (auto* sta = reg.try_get<Stamina>(p))
        sta->current = sta->max_stamina;
    if (auto* charge = reg.try_get<Charge>(p))
        charge->current = charge->max_charge;
    thermos::rest(em); // he wakes as if he had taken his break -- flask full
    poe::log().info("death: he wakes at home");
}

// EVERYTHING HE WAS IN THE MIDDLE OF, ENDED. The world can be taken away from him mid-act --
// he dies, a door draws its curtain, he opens the pause screen -- and each of those stops the
// tick outright. A stopped tick does not finish what is in flight, it PRESERVES it: a flash
// holds at full brightness, a tint freezes on the body, a held stream goes on hissing behind
// the black. So the moment the world stops being his, the things happening in it are ended
// rather than paused.
//
// Each module takes its own away; this only knows the roll call.
void standDown(EntityManager& em)
{
    tools::holster(em); // the stream, its head, and the trigger it was held with
    hit_area::forget(em);
    tint::forget(em);
    floaters::clear();
    notify::clear();
}

// THE WORLD IS HIS only while nothing has taken it: no black over it, no curtain, no screen he
// has stepped into. Asked rather than listed at each transition -- there are several ways to
// lose it, and a stand-down called at each is a list that one day misses one.
bool worldIsHis()
{
    return sDeathBeat == DeathBeat::None && !travel::active() && !sApp.staging;
}

// WHEN HE ASKED TO GO. The teardown after the loop is only half the story of a slow quit --
// whatever the last frame does before the loop notices is the other half, and a save is written
// there.
std::chrono::steady_clock::time_point sQuitAsked{};
void askedToQuit()
{
    sQuitAsked = std::chrono::steady_clock::now();
}
long long sinceQuitAsked()
{
    return sQuitAsked.time_since_epoch().count() == 0
               ? -1
               : std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - sQuitAsked)
                     .count();
}

// Ends what is in flight on the tick he loses the world, and only on that tick -- a stand-down
// run every frame would silence a stream he is still holding.
void noticeTheWorldLeavingHim(EntityManager& em, bool nowPlaying)
{
    static bool sHad = false;
    const bool has = nowPlaying && worldIsHis();
    if (sHad && !has)
        standDown(em);
    sHad = has;
}

// The world only ticks while it is being PLAYED: not behind the title, and not behind the
// pause screen. Pausing is a thing the world does, not a phase the program enters, so it is a
// flag checked here rather than a separate branch of the shell.
std::string sWrittenArea;
int sWrittenNode = -1;

// WHAT SPACE MEANS WHERE HE IS STANDING -- one gesture, one meaning per square, decided by
// what is underfoot. True when the world was torn down and rebuilt, which ends the tick.
bool offerUnderfoot(Engine& engine, EntityManager& em)
{
    // THE STAGING AREA, the reference's interaction shape: in range, either Space (the interact
    // key) or a click ON the spot itself. Interacting IS resting -- heal, refill, and the staging
    // menu opens where you choose the brew. The click is swallowed so putting the kit down
    // never doubles as a trigger pull.
    // ONE INTERACT GESTURE: stand on the spot, press Space. The mouse is the
    // weapon's hand and never doubles as a use key -- clicking or hovering a
    // spot means nothing.
    if (reward::atRest(em))
    {
        prompt::offer("Rest");
        if (player::consumeInteract())
        {
            thermos::rest(em);
            sApp.staging = true;
            staging_screen::reset();
            aim::requireFreshPress();
            return true;
        }
    }
    else if (const auto& at = em.registry().get<Transform>(player::entity());
             descent::openableUnderfoot(em, at.x, at.y) >= 0)
    {
        // BREAKING IT OPEN is his act. A floor answers being disturbed, so nothing presses
        // until he decides which hole to disturb -- and he can read the room first.
        prompt::offer("Open");
        if (player::consumeInteract())
        {
            descent::open(em, descent::openableUnderfoot(em, at.x, at.y));
            aim::requireFreshPress();
        }
    }
    else if (const entt::entity through = passageUnderfoot(em);
             through != entt::null && !em.registry().get<PlacedHole>(through).in_use)
    {
        // TAKING A PASSAGE IS DELIBERATE, never a walk-on: you do not fall into the wound by
        // accident, and the way he came in obeys the same rule going the other way.
        // WHERE, not what: "B1-A -> B2-A" says which way this goes, which no single verb can
        // once a hole in a wall opens a room at the depth he is already on.
        const int hole = em.registry().get<PlacedHole>(through).hole;
        prompt::offerStep(descent::beyondLabel(hole), descent::stepDir(hole));
        if (player::consumeInteract())
        {
            // Behind the curtain, exactly as a door is: the world is torn down and rebuilt,
            // and that is not something to show him.
            travel::cut([&engine, &em, hole] { descent::travel(engine, em, hole); });
            aim::requireFreshPress();
            return true;
        }
    }
    else
        player::consumeInteract(); // a Space pressed in the field means nothing yet -- drop it
    return false;
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    // Entering play swallows whatever the trigger was doing on the menu. Derived from the state
    // rather than called at each transition -- there are several ways in (take a job, resume,
    // back out of settings) and one of them would eventually forget.
    static bool sWasPlaying = false;
    const bool nowPlaying = sApp.phase == app::Phase::Playing && !sApp.paused && !sApp.staging;
    if (nowPlaying && !sWasPlaying)
        aim::requireFreshPress();
    sWasPlaying = nowPlaying;

    noticeTheWorldLeavingHim(em, nowPlaying);

    if (!nowPlaying)
        return;

    // WHAT IS TRUE about the room, as opposed to what is moving in it. Settled
    // at the top of every frame, including the frames a cut owns: a state that
    // stood down with the rest of the tick would go on describing the room he
    // left while he is already looking at the one he arrived in.
    descent::update(engine, em, static_cast<float>(dt));
    zone::update(em, static_cast<float>(dt), travel::active() || sDeathBeat != DeathBeat::None);

    // WRITTEN DOWN WHENEVER THE GROUND CHANGES. A room crossed, a floor dug or
    // climbed: that is when the things a save keeps actually move, and it costs
    // a small document. Quitting is therefore never a thing he has to remember
    // to do -- there is no save verb anywhere in this game.
    if (const std::string& here = travel::currentArea();
        here != sWrittenArea || descent::standing() != sWrittenNode)
    {
        sWrittenArea = here;
        sWrittenNode = descent::standing();
        if (sApp.world_built)
            save_ops::persist(em);
    }

    // The death beat owns the clock while it runs: the world holds its breath, the floor is
    // flooded back under cover of the black, and play resumes only once the fade-in ends.
    if (sDeathBeat != DeathBeat::None)
    {
        sDeathTimer += static_cast<float>(dt);
        if (sDeathBeat == DeathBeat::FadeOut && sDeathTimer >= kDeathFadeOut)
        {
            // Full black: he goes home under it, no questions asked.
            deathReturn(engine, em);
            sDeathBeat = DeathBeat::FadeIn;
            sDeathTimer = 0.0f;
        }
        else if (sDeathBeat == DeathBeat::FadeIn && sDeathTimer >= kDeathFadeIn)
        {
            sDeathBeat = DeathBeat::None;
            aim::requireFreshPress(); // a trigger held through death is not an order to fire
        }
        return;
    }

    // A door transition owns the clock exactly as the death beat does: the
    // world holds its breath under the black and the swap happens mid-curtain.
    if (travel::active())
    {
        travel::update(engine, em, static_cast<float>(dt));
        return;
    }

    // Aim first: everything that fires this tick reads where he is pointing, and a stale
    // cursor would put the shot where he pointed last frame.
    aim::update(engine, em);
    player::update(engine, em, dt);

    // After movement, before anything reads the map: crossing a door starts
    // the curtain and the rest of this tick stands down.
    travel::update(engine, em, static_cast<float>(dt));
    if (travel::active())
        return;

    if (offerUnderfoot(engine, em))
        return;

    // The field is rebuilt toward the player, then everything reads it -- see Chase.h.
    const auto& pt = em.registry().get<Transform>(player::entity());
    FlowFieldSystem::update(em, pt.x, pt.y);
    chase::update(em, static_cast<float>(dt));
    swarm::update(em, static_cast<float>(dt));

    // THE TRADE HAS ITS PLACE: the weapon fires only where pest can reach
    // him. At the bar he is a man carrying equipment, not a man spraying it --
    // and holstering is EXPLICIT, because a stream interrupted by death or a
    // door would otherwise hold its state (and his facing) forever.
    if (zone::combat())
        tools::update(em, static_cast<float>(dt));
    else
        tools::holster(em);
    tools::tickStamina(em, static_cast<float>(dt));
    tools::tickParticles(em, static_cast<float>(dt));
    hit_area::update(em, static_cast<float>(dt));
    reward::update(em, static_cast<float>(dt));
    pickup::update(em);
    floaters::update(static_cast<float>(dt));
    // AFTER the movement that chooses which animation plays, so a frame shows the pose that
    // matches where the character now is rather than trailing it by a tick. Paused above, so a
    // character stops mid-stride instead of walking on the spot behind the menu.
    AnimationSystem::update(em, static_cast<float>(dt));
    // After everything that moves, since the gait is driven by how far things actually went.
    walk_bob::update(em, static_cast<float>(dt));

    // Last of all, so every colour is decided from what the tick actually left behind rather
    // than from what anything meant to happen.
    tint::update(em);

    // After every system that could have hurt him this tick.
    if (const auto* hp = em.registry().try_get<Health>(player::entity());
        hp != nullptr && hp->current <= 0 && sDeathBeat == DeathBeat::None)
    {
        sDeathBeat = DeathBeat::FadeOut;
        sDeathTimer = 0.0f;
        sound::play("death");
    }
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float /*alpha*/)
{
    // Draw the world at the internal resolution, then blit the whole buffer up. Zoom stays
    // at 1: the scale comes from the upscale, not from the camera, which is what keeps every
    // pixel square and every rounding decision in one place.
    // A zoom change (or a resize) re-makes the target at the new size. Cheap, and it happens
    // only when the number actually moves.
    static int sLastZoom = 0;
    static int sLastW = 0;
    if (debug_panel::zoom() != sLastZoom || engine.windowWidth() != sLastW)
    {
        sLastZoom = debug_panel::zoom();
        sLastW = engine.windowWidth();
        engine::gl::pixelTargetInit(internalW(engine), internalH(engine));
        engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
        RenderSystem::resize(internalW(engine), internalH(engine));
    }

    engine::gl::pixelTargetBegin(kVoidR, kVoidG, kVoidB);
    if (sApp.world_built)
    {
        TileMapRenderer::render(camX, camY, internalW(engine), internalH(engine), 1.0f);
        // BETWEEN THE GROUND AND THE BODIES ON IT. The ground layer is what a thing stands on;
        // decoration is what is drawn onto it -- the fronts of walls, and whatever is stamped on
        // a floor later -- and he walks in front of all of it.
        TileMapRenderer::renderDecoration(camX, camY, internalW(engine), internalH(engine), 1.0f);
        RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
    }
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());

    // Captures the WORLD only. The shell is drawn by the UI callback, which the engine runs
    // inside UIRenderer begin/endFrame -- its text is still unsubmitted when any game callback
    // returns, so a menu cannot be grabbed from here.
    capture::writeIfRequested(engine.windowWidth(), engine.windowHeight());
}

// GOING TO WORK, which is one act whether or not he has been before: put the
// world up, then put back whatever a previous sitting wrote down. There is no
// second entry point for "continue" because there is nothing for the player to
// decide -- either the disk has a job on it or it does not.
bool buildWorld(Engine& engine)
{
    auto& em = engine.entityManager();
    em.registry().clear();
    descent::reset();
    zone::reset();
    record::reset();
    hud::reset(); // the readout remembers one sitting, and this is a new one
    loadConfigs();
    ensurePlayer(em, 0.0f, 0.0f);
    if (save_ops::resume(engine, em))
    {
        sApp.world_built = true;
        return true;
    }
    if (const std::string start = area::startLevel("assets/maps/world.ldtk"); !start.empty())
    {
        if (travel::enter(engine, em, start))
        {
            sApp.world_built = true;
            save_ops::persist(em); // a first morning is a job like any other
            return true;
        }
    }
    poe::log().error("world: no start level -- a job needs somewhere to begin");
    return false;
}

// What the shell does with what a screen reported.
void enactTitle(Engine& engine, title_screen::Action a)
{
    switch (a)
    {
    case title_screen::Action::Work:
        if (buildWorld(engine))
            sApp.phase = app::Phase::Playing;
        break;
    case title_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Title;
        sApp.phase = app::Phase::Settings;
        settings_screen::reset();
        break;
    case title_screen::Action::Quit:
        askedToQuit();
        engine.requestQuit();
        break;
    case title_screen::Action::None:
        break;
    }
}

void enactPause(Engine& engine, pause_screen::Action a)
{
    // Anything that ends the sitting writes first: leaving for the title and
    // closing the game are both the last moment his work still exists in memory.
    if (a == pause_screen::Action::Leave || a == pause_screen::Action::Quit)
        save_ops::persist(engine.entityManager());
    switch (a)
    {
    case pause_screen::Action::Resume:
        sApp.paused = false;
        break;
    case pause_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Playing;
        sApp.phase = app::Phase::Settings;
        settings_screen::reset();
        break;
    case pause_screen::Action::Leave:
        sApp.paused = false;
        sApp.world_built = false;
        sApp.phase = app::Phase::Title;
        travel::reset();
        descent::reset();
        title_screen::reset();
        break;
    case pause_screen::Action::Quit:
        askedToQuit();
        engine.requestQuit();
        break;
    case pause_screen::Action::None:
        break;
    }
}

// The world's own layer of the HUD -- anchored to the camera the world was drawn with, and the
// two black curtains that cover a changeover, which fade the HUD along with everything else.
void renderPlayOverlays(Engine& engine, EntityManager& em)
{
    // World-anchored, so it needs the camera the world was drawn with.
    const auto& cam = em.registry().get<Camera>(player::entity());
    const float cx = std::round(cam.x);
    const float cy = std::round(cam.y);
    hud::renderWorldOverlays(engine, em, cx, cy, debug_panel::zoom());
    floaters::render(engine, cx, cy, debug_panel::zoom());
    hud::render(engine, em);

    // The black, over everything -- the HUD fades with the world.
    if (sDeathBeat != DeathBeat::None)
    {
        const float a = (sDeathBeat == DeathBeat::FadeOut) ? sDeathTimer / kDeathFadeOut
                                                           : 1.0f - sDeathTimer / kDeathFadeIn;
        UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(engine.windowWidth()),
                             static_cast<float>(engine.windowHeight()),
                             screen_style::black(std::min(1.0f, std::max(0.0f, a))));
    }
    // The door curtain, same cloth as the death black.
    if (travel::active())
        UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(engine.windowWidth()),
                             static_cast<float>(engine.windowHeight()),
                             screen_style::black(travel::curtainAlpha()));
}

// WHICH SURFACE IS UP follows from the phase; pausing is a flag on Playing rather than a phase
// of its own, because the world is still loaded behind it. Keyboard is read first and the mouse
// second over what was drawn -- either may commit, neither twice.
void runShellPhase(Engine& engine, EntityManager& em, const shell_input::Frame& in, int ww, int wh)
{
    switch (sApp.phase)
    {
    case app::Phase::Title:
    {
        // Keyboard first, then the mouse over what was drawn -- either may commit.
        enactTitle(engine, title_screen::step(in.up, in.down, in.confirm));
        if (sApp.phase == app::Phase::Title)
            enactTitle(engine, title_screen::render(in.mouse, ww, wh));
        break;
    }

    case app::Phase::Settings:
    {
        // Erasing the file is offered from the TITLE only: there is no job running there to
        // write itself straight back over the erasure.
        const bool fromTitle = sApp.settings_return_to == app::Phase::Title;
        settings_screen::Action a = settings_screen::step(in.up, in.down, in.confirm, in.back);
        if (a == settings_screen::Action::None)
            a = settings_screen::render(in.mouse, fromTitle, ww, wh);
        if (a == settings_screen::Action::Forget)
        {
            save_ops::forget();
            poe::log().info("save: the file is closed -- nothing to go back to");
        }
        if (a == settings_screen::Action::Back)
            sApp.phase = sApp.settings_return_to;
        break;
    }
    case app::Phase::Playing:
        if (sApp.staging)
        {
            staging_screen::Action a = staging_screen::step(in.up, in.down, in.confirm, in.back);
            if (a == staging_screen::Action::None)
                a = staging_screen::render(em, in.mouse, ww, wh);
            if (a == staging_screen::Action::Close)
                sApp.staging = false;
            break;
        }
        if (!sApp.paused && in.menu)
        {
            sApp.paused = true;
            // Kills land between rooms and the record is the one thing that is
            // his rather than the job's -- so stopping counts as a stopping
            // point, and quitting from here can never cost him the morning.
            save_ops::persist(em);
            pause_screen::reset();
        }
        else if (sApp.paused)
        {
            enactPause(engine,
                       pause_screen::step(in.up, in.down, in.left, in.right, in.confirm, in.back));
            if (sApp.paused)
                enactPause(engine,
                           pause_screen::render(em, engine.textureManager(), in.mouse, ww, wh));
        }
        break;
    }
}

// The shell, drawn over everything.
void gameRenderUI(Engine& engine, EntityManager& em)
{
    const bool playing = sApp.phase == app::Phase::Playing && !sApp.paused && !sApp.staging;
    // The dev panel needs a pointer to click, and it opens while playing -- so it counts as a
    // screen with options, exactly like a menu. The system cursor gives way ONLY where the
    // crosshair takes its place.
    hud::cursorForPhase(playing && !debug_panel::visible() && zone::combat());
    if (playing)
        renderPlayOverlays(engine, em);

    const shell_input::Frame in = shell_input::read(em.mouse_wheel_y);
    em.mouse_wheel_y = 0;
    runShellPhase(engine, em, in, engine.windowWidth(), engine.windowHeight());
}

void gameOnResize(Engine& /*engine*/, int w, int h)
{
    // The blit follows the window, and so does the internal size -- see internalW().
    engine::gl::pixelTargetResize(w, h);
}

} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

#ifdef POE_SOURCE_DIR
    // Dev builds run against the source tree: every relative path (config/,
    // assets/) resolves there, and the editor's saves land where git sees them.
    std::error_code ec;
    std::filesystem::current_path(POE_SOURCE_DIR, ec);
#endif

    Engine engine;
    // BORDERLESS FULLSCREEN, and it is a performance decision as much as a presentation one.
    // In a WINDOW the Windows compositor (DWM) owns the present: it caps the swap regardless
    // of what SDL_GL_SetSwapInterval asks for, delivering ~57 fps against a 60 Hz tick with
    // periodic 47-76 ms stalls. The fixed-timestep accumulator then slips a tick every so
    // often and the world visibly hitches -- while the game itself renders in 0.04 ms.
    // Going borderless bypasses the compositor and gets a real present. (Wayworn does the
    // same; prison-escape does not, and shows the same hitch.)
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);
    // FIRST, before anything can fault: a crash that happens earlier than this
    // is a crash nobody gets to read about.
    engine::crash::install("poe");
    poe::log().info("boot: crash handler armed");

    if (!engine.init("Point of Entry", 1280, 720))
        return 1;

    poe::log().info("boot: window is {}x{}", engine.windowWidth(), engine.windowHeight());
    engine::gl::pixelTargetInit(internalW(engine), internalH(engine));
    engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
    RenderSystem::init(internalW(engine), internalH(engine));
    TileMapRenderer::init();
    // The engine enables depth testing globally at init for the 3D path. This is a 2D
    // game: everything draws at z=0, so with GL_LESS the tilemap writes depth first and
    // every sprite fails the test and vanishes. Depth order here is the painter's
    // algorithm (RenderSystem sorts by Y), so the test must be off.
    //
    // Worth knowing: the other 2D games only avoid this by ACCIDENT -- UIRenderer
    // disables depth when it submits a batch, and they draw a HUD every frame. A game
    // with no HUD yet hits it immediately, and every diagnostic still reads as correct.
    glDisable(GL_DEPTH_TEST);

    // The one face at its two ladder sizes -- ScreenStyle owns fonts and the integer scale
    // (see check_design.py for the standards this keeps).
    screen_style::initFonts(engine.windowHeight());

    // Authored places: builders first, then the door index over the project.
    travel::init();
    area_build::registerAll();
    travel::scan("assets/maps/world.ldtk");

    auto& em = engine.entityManager();

    engine.setGameUpdate(&gameUpdate);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setOnResize(&gameOnResize);
    engine.setRenderUI(&gameRenderUI);
    engine.setRenderImGui(&debug_panel::render);
    engine.run();

    // TIMED, because quitting has hung before and a hang with no output is a bug nobody can
    // place. The window is already gone by then, so the only account of it is this line -- and
    // "it felt slow" is not one. The engine's own teardown is taken here rather than left to the
    // destructor so it falls inside the measurement.
    poe::log().info("quit: loop ended {} ms after it was asked for", sinceQuitAsked());
    const auto began = std::chrono::steady_clock::now();
    engine::gl::pixelTargetShutdown();
    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    const auto mine = std::chrono::steady_clock::now();
    engine.shutdown();
    const auto done = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b)
    { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    poe::log().info("quit: clean -- renderers {} ms, engine {} ms", ms(began, mine),
                    ms(mine, done));
    return 0;
}
