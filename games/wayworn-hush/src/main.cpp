#include "Engine.h"
#include "GameLoop.h"
#include "Version.h"
#include "WorldInit.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>

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

    // World-render subsystems (game-owned; mirror the renderWorld callback).
    // RenderSystem draws sprites into the internal-res pixel target, so it is
    // sized to the internal resolution, not the window.
    TileMapRenderer::init();
    RenderSystem::init(kInternalWidth, kInternalHeight);

    world_init::buildPlaceholderRegion(em);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    gameSetPlayer(world_init::spawnPlayer(em));

    engine.setGameUpdate(&gameUpdate);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setRenderUI(&gameRenderUI);
    engine.run();

    RenderSystem::shutdown();
    TileMapRenderer::shutdown();
    engine::gl::pixelTargetShutdown();
    return 0;
}
