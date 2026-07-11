#include "Engine.h"
#include "FontManager.h"
#include "GameLoop.h"
#include "Glimmer.h"
#include "ThoughtBox.h"
#include "Version.h"
#include "WorldInit.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/AudioSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>

// ---------------------------------------------------------------------------
// Crash reporter -- writes crash.log next to the exe on fatal signals /
// unhandled exceptions. (No save-dir resolution yet; the save framework
// lands in a later slice step.)
// ---------------------------------------------------------------------------

static void writeCrashLog(const char* reason)
{
    FILE* f = fopen("crash.log", "w");
    if (!f)
        return;

    const time_t t = time(nullptr);
    char timebuf[64] = {};
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));

    fprintf(f, "crashed at %s\n", timebuf);
    fprintf(f, "cause:     %s\n", reason);
    fclose(f);
}

static void signalHandler(int sig)
{
    const char* name = "unknown signal";
    if (sig == SIGSEGV)
        name = "SIGSEGV (segmentation fault)";
    else if (sig == SIGABRT)
        name = "SIGABRT (abort / assert)";
    else if (sig == SIGFPE)
        name = "SIGFPE (floating-point exception)";
    else if (sig == SIGILL)
        name = "SIGILL (illegal instruction)";
    writeCrashLog(name);
    _exit(1);
}

static void terminateHandler()
{
    static char buf[256] = "std::terminate (no active exception)";
    try
    {
        throw;
    }
    catch (const std::exception& e)
    {
        snprintf(buf, sizeof(buf), "unhandled exception: %s", e.what());
    }
    catch (...)
    {
        snprintf(buf, sizeof(buf), "unhandled exception (unknown type)");
    }
    writeCrashLog(buf);
    _exit(1);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
    std::set_terminate(terminateHandler);

    Engine engine;

    // 1536x864 = exactly 2x the 768x432 internal target: clean fullscreen blit,
    // no letterbox at the default window size.
    if (!engine.init("Wayworn Hush v" GAME_VERSION, 1536, 864))
        return 1;

    auto& em = engine.entityManager();

    // This game's world grid: 32px tiles. Sized to the 32x64 protagonist sprite
    // so on-screen scale stays EarthBound/Pokemon (~1 tile wide, 2 tall) at the
    // sprite's pixel density. See docs/design/SCALE.md.
    em.tile_map.tile_size = 32;

    // The engine clear fills the window (letterbox bars); the pixel target
    // clears the internal image to the same color -- so the two agree.
    engine.setClearColor(kAmbientR, kAmbientG, kAmbientB);

    // Pixel-perfect upscaling target: world renders at internal res, blits up.
    engine::gl::pixelTargetInit(kInternalWidth, kInternalHeight);
    engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
    engine.setOnResize([](Engine&, int w, int h) { engine::gl::pixelTargetResize(w, h); });

    // Global color grade -- pulls the scene toward the muted aesthetic. Loaded
    // from config so the mood is tunable live (edit config/atmosphere.json +
    // rebuild, no recompile).
    if (std::ifstream af{"config/atmosphere.json"})
    {
        const auto aj = nlohmann::json::parse(af, nullptr, false);
        if (!aj.is_discarded())
        {
            const auto& g = aj.value("grade", nlohmann::json::object());
            engine::gl::Grade grade;
            grade.saturation = g.value("saturation", 1.0f);
            grade.brightness = g.value("brightness", 1.0f);
            grade.tint_r = g.value("tint_r", 1.0f);
            grade.tint_g = g.value("tint_g", 1.0f);
            grade.tint_b = g.value("tint_b", 1.0f);
            engine::gl::pixelTargetSetGrade(grade);
        }
    }

    // World-render subsystems (game-owned; mirror the renderWorld callback).
    // RenderSystem draws sprites into the internal-res pixel target, so it is
    // sized to the internal resolution, not the window.
    TileMapRenderer::init();
    RenderSystem::init(kInternalWidth, kInternalHeight);

    world_init::buildPlaceholderRegion(em);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    GameState& gs = em.registry().ctx().emplace<GameState>();
    gs.player_config = loadPlayerConfig("config/player.json");
    gs.player = world_init::spawnPlayer(em, gs.player_config);
    observations::load(gs.observations, "config/observations.json");
    glimmer::spawn(em, gs.observations);

    // Placeholder UI font (a serif stand-in -- the real pixel font is a later
    // aesthetic-pass choice; see docs/design/AESTHETIC.md UI style).
    const FontHandle uiFont = FontManager::loadFont("assets/fonts/placeholder.ttf", 48.0f);
    thought_box::init(uiFont);

    // The region's ambient bed. Loops with a slow fade-in so the world eases in
    // rather than snapping on. Low volume -- the score is sparse and unhurried
    // (see docs/design/AESTHETIC.md). Runs silent if no audio device.
    // (AudioSystem's init/shutdown are owned by the engine; the game only
    // decides what to play.)
    AudioSystem::playMusic("assets/audio/ambient_meadow.ogg", 0.55f, /*loop=*/true,
                           /*fade_in_ms=*/3000);

    engine.setGameUpdate(&gameUpdate);
    engine.setPreRender(&gamePreRender);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setRenderUI(&gameRenderUI);
    engine.run();

    RenderSystem::shutdown();
    TileMapRenderer::shutdown();
    engine::gl::pixelTargetShutdown();
    return 0;
}
