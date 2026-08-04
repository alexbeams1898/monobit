#include "AppState.h"
#include "Capture.h"
#include "DebugPanel.h"
#include "Engine.h"
#include "FloorGen.h"
#include "FontManager.h"
#include "Log.h"
#include "PauseScreen.h"
#include "Player.h"
#include "ScreenStyle.h"
#include "ShellInput.h"
#include "TitleScreen.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <cmath>

#include <glad/glad.h>

// Point of Entry -- the skeleton. Right now it does one thing: build a floor out
// of hand-authored ASCII rooms, put the player in it, and let him walk around.
// Everything else (the gadget, sealing, the swarm) arrives on top of this.

namespace
{
// THE INTERNAL RESOLUTION. Everything renders here at zoom 1 and the whole buffer is
// then scaled to the window in one pass -- the pixel-art pipeline. Two reasons it is
// this and not the window:
//
//   1. It IS the lo-fi look. 640x360 with 16px tiles shows 40x22 tiles: chunky enough
//      to read as pixel art, wide enough for a swarm and a cursor. (GBC is 160x144 and
//      NES 256x240 -- both authentic and both too claustrophobic to fight in.)
//   2. One rounding pass. Drawing straight to the window means the camera and every
//      sprite round to a pixel independently, and at a zoom those errors do not cancel
//      -- things pop against each other whenever frame times jitter, which reads as lag.
//
// 1280x720 lands on an EXACT 2x at 2560x1440 and 1.5x at 1080p. The earlier 640x360 was
// chosen for chunkiness and turned out to be a 4x upscale on a 1440p panel -- every 16px
// tile drawing as 64 real pixels, which reads as being zoomed most of the way in and makes
// ordinary walking speed feel frantic. The lo-fi look comes from the ART (16px tiles, flat
// colour, few frames), not from starving the framebuffer.
constexpr int kInternalWidth = 1280;
constexpr int kInternalHeight = 720;

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

// A box that reads as a thing, with no art yet.
entt::entity spawnBox(EntityManager& em, float x, float y, float size, float r, float g, float b)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    // Transform is {x, y, z, rotation, pitch, roll, scale} -- set the fields by
    // name rather than by position, or a mis-counted brace silently lands in the
    // wrong one (scale 0 draws a sprite zero pixels wide, which looks exactly
    // like the sprite not existing).
    Transform t{};
    t.x = x;
    t.y = y;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    Sprite spr{};
    spr.src_w = static_cast<int>(size);
    spr.src_h = static_cast<int>(size);
    spr.layer = 2;
    reg.emplace<Sprite>(e, spr);
    // No art yet: the renderer draws a flat quad for a SolidColor sprite, which
    // is all the skeleton needs to prove placement.
    reg.emplace<SolidColor>(e, SolidColor{r, g, b});
    return e;
}

// The world only ticks while it is being PLAYED: not behind the title, and not behind the
// pause screen. Pausing is a thing the world does, not a phase the program enters, so it is a
// flag checked here rather than a separate branch of the shell.
void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    if (sApp.phase != app::Phase::Playing || sApp.paused)
        return;
    player::update(engine, em, dt);
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float /*alpha*/)
{
    // Draw the world at the internal resolution, then blit the whole buffer up. Zoom stays
    // at 1: the scale comes from the upscale, not from the camera, which is what keeps every
    // pixel square and every rounding decision in one place.
    engine::gl::pixelTargetBegin(kVoidR, kVoidG, kVoidB);
    if (sApp.world_built)
    {
        TileMapRenderer::render(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
        RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
    }
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());

    // Captures the WORLD only. The shell is drawn by the UI callback, which the engine runs
    // inside UIRenderer begin/endFrame -- its text is still unsubmitted when any game callback
    // returns, so a menu cannot be grabbed from here.
    capture::writeIfRequested(engine.windowWidth(), engine.windowHeight());
}

// Build the house. Not done at boot: the title must be able to greet you without a floor
// existing, and a new job should be a NEW house rather than the one generated at startup.
bool buildWorld(Engine& engine)
{
    auto& em = engine.entityManager();
    em.registry().clear(); // a previous job's world, if any

    const floorgen::Floor floor = floorgen::generate(em, "config/floor.json", "config/rooms");
    if (!floor.ok)
    {
        poe::log().error("world: could not build a floor");
        return false;
    }
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Every marker the rooms carried. 'P' is a point of entry -- for now a red box standing in
    // the room, which is enough to prove placement works.
    int poeCount = 0;
    for (const auto& m : floor.markers)
        if (m.type == 'P')
        {
            spawnBox(em, m.x, m.y, kPoeSize, 0.75f, 0.15f, 0.15f);
            ++poeCount;
        }

    const entt::entity playerEnt =
        spawnBox(em, floor.spawn_x, floor.spawn_y, kPlayerSize, 0.85f, 0.84f, 0.78f);
    em.registry().emplace<Camera>(playerEnt, Camera{floor.spawn_x, floor.spawn_y});
    player::bind(playerEnt);
    poe::log().info("world: player at ({:.0f},{:.0f}), {} points of entry", floor.spawn_x,
                    floor.spawn_y, poeCount);
    sApp.world_built = true;
    return true;
}

// What the shell does with what a screen reported.
void enactTitle(Engine& engine, title_screen::Action a)
{
    switch (a)
    {
    case title_screen::Action::NewJob:
    case title_screen::Action::Continue: // no saves yet; both mean "go in" for now
        if (buildWorld(engine))
            sApp.phase = app::Phase::Playing;
        break;
    case title_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Title;
        sApp.phase = app::Phase::Settings;
        break;
    case title_screen::Action::Quit:
        engine.requestQuit();
        break;
    case title_screen::Action::None:
        break;
    }
}

void enactPause(Engine& engine, pause_screen::Action a)
{
    switch (a)
    {
    case pause_screen::Action::Resume:
        sApp.paused = false;
        break;
    case pause_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Playing;
        sApp.phase = app::Phase::Settings;
        break;
    case pause_screen::Action::Leave:
        sApp.paused = false;
        sApp.world_built = false;
        sApp.phase = app::Phase::Title;
        title_screen::reset();
        break;
    case pause_screen::Action::Quit:
        engine.requestQuit();
        break;
    case pause_screen::Action::None:
        break;
    }
}

// The shell, drawn over everything. Which surface is up follows from the phase; pausing is a
// flag on Playing rather than a phase, because the world is still loaded behind it.
void gameRenderUI(Engine& engine, EntityManager& /*em*/)
{
    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();

    const shell_input::Frame in = shell_input::read();

    switch (sApp.phase)
    {
    case app::Phase::Title:
    {
        const title_screen::Mouse m{in.mouse_x, in.mouse_y, in.clicked};
        // Keyboard first, then the mouse over what was drawn -- either may commit.
        enactTitle(engine, title_screen::step(in.up, in.down, in.confirm, /*has_save=*/false));
        if (sApp.phase == app::Phase::Title)
            enactTitle(engine, title_screen::render(m, /*has_save=*/false, ww, wh));
        break;
    }
    case app::Phase::Settings:
        // No settings surface yet: anything that leaves goes back where it came from.
        screen_style::dim(ww, wh);
        screen_style::headingCentered("SETTINGS", static_cast<float>(ww) * 0.5f,
                                      static_cast<float>(wh) * 0.35f, screen_style::kText);
        screen_style::textCentered(
            "nothing to set yet -- Esc goes back", static_cast<float>(ww) * 0.5f,
            static_cast<float>(wh) * 0.35f + screen_style::lineHeight() * 2.0f,
            screen_style::kTextDim);
        if (in.back || in.confirm)
            sApp.phase = sApp.settings_return_to;
        break;
    case app::Phase::Playing:
        if (!sApp.paused && in.back)
        {
            sApp.paused = true;
            pause_screen::reset();
        }
        else if (sApp.paused)
        {
            const pause_screen::Mouse m{in.mouse_x, in.mouse_y, in.clicked};
            enactPause(engine, pause_screen::step(in.up, in.down, in.confirm, in.back));
            if (sApp.paused)
                enactPause(engine, pause_screen::render(m, ww, wh));
        }
        break;
    }
}

void gameOnResize(Engine& /*engine*/, int w, int h)
{
    // Only the BLIT follows the window; the world keeps rendering at the internal size.
    engine::gl::pixelTargetResize(w, h);
}
} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Engine engine;
    // BORDERLESS FULLSCREEN, and it is a performance decision as much as a presentation one.
    // In a WINDOW the Windows compositor (DWM) owns the present: it caps the swap regardless
    // of what SDL_GL_SetSwapInterval asks for, delivering ~57 fps against a 60 Hz tick with
    // periodic 47-76 ms stalls. The fixed-timestep accumulator then slips a tick every so
    // often and the world visibly hitches -- while the game itself renders in 0.04 ms.
    // Going borderless bypasses the compositor and gets a real present. (Wayworn does the
    // same; prison-escape does not, and shows the same hitch.)
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);
    if (!engine.init("Point of Entry", 1280, 720))
        return 1;

    poe::log().info("boot: window is {}x{}", engine.windowWidth(), engine.windowHeight());
    engine::gl::pixelTargetInit(kInternalWidth, kInternalHeight);
    engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
    RenderSystem::init(kInternalWidth, kInternalHeight);
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

    // The shell's two type sizes, scaled off the window so the menus read the same on any
    // display. Loaded here rather than per-screen so both surfaces share one face.
    {
        const float s = static_cast<float>(engine.windowHeight());
        const FontHandle body = FontManager::loadFont("assets/fonts/placeholder.ttf", s * 0.022f);
        const FontHandle heading =
            FontManager::loadFont("assets/fonts/placeholder.ttf", s * 0.045f);
        poe::log().info("boot: fonts body={} heading={} (size {:.1f}/{:.1f})", body, heading,
                        s * 0.022f, s * 0.045f);
        screen_style::init(body, heading);
    }

    auto& em = engine.entityManager();

    engine.setGameUpdate(&gameUpdate);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setOnResize(&gameOnResize);
    engine.setRenderUI(&gameRenderUI);
    engine.setRenderImGui(&debug_panel::render);
    engine.run();

    engine::gl::pixelTargetShutdown();
    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    return 0;
}
