#include "ConfigLoader.h"
#include "Engine.h"
#include "GameLoop.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "systems/LevelingSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/WaveSystem.h"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>

// ---------------------------------------------------------------------------
// Crash reporter — writes crash.log when the process dies unexpectedly.
// Keeps the file minimal: timestamp + cause. No game state yet; add once
// the save system exists so there is something worth preserving.
// ---------------------------------------------------------------------------

// Not async-signal-safe to use fopen/fprintf in a signal handler, but for a
// crash reporter "best effort" beats "nothing" — the process is dead anyway.
static void writeCrashLog(const char* reason)
{
    FILE* f = fopen("crash.log", "w");
    if (!f)
        return;

    time_t t = time(nullptr);
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
    // Best-effort: re-throw inside terminate to recover the exception message.
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

    if (!engine.init("Hell Escape", 1280, 720))
        return 1;

    auto& em = engine.entityManager();

    // Load balance formulas first — all systems read from em.formulas.
    ConfigLoader::loadFormulas(em, "config/balance/formulas.json");

    // Load sound mappings — all systems read from em.sounds.
    ConfigLoader::loadSounds(em, "config/audio/sounds.json");

    // Load wave definitions — WaveSystem reads from em.wave_config.
    ConfigLoader::loadWaves(em, "config/waves.json");

    // Generate the tile map — populates em.tile_map / em.tile_config and
    // returns the world-space centre of the first placed room (player spawn).
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms");
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Player — placed at the first room's centre.
    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = px;
        t.y = py;
    }

    // Spawn non-enemy entities from tile map markers.
    // 'R' → rest spot. Enemy spawning is handled by WaveSystem.
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type != 'R')
            continue;

        auto entity = ConfigLoader::loadEntity(em, "config/entities/rest_spot.json");
        if (!em.registry().valid(entity))
            continue;

        auto& t = em.registry().get<Transform>(entity);
        t.x = sp.x;
        t.y = sp.y;
    }

    if (em.registry().valid(player))
    {
        em.registry().emplace<Input>(player);
        const auto& pt = em.registry().get<Transform>(player);
        em.registry().emplace<Camera>(player, Camera{pt.x, pt.y, true});
    }

    // Derive Health.max from END stats for all stat-based entities.
    LevelingSystem::applyInitialDerivations(em);

    // Auto-start wave 1 so enemies begin spawning immediately.
    WaveSystem::startNextWave(em);

    engine.setGameUpdate(&gameUpdate);
    engine.run();
    return 0;
}
