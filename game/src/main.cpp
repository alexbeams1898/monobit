#include "ConfigLoader.h"
#include "Engine.h"
#include "GameLoop.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
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

    if (!engine.init("Hell Escape", 1920, 1080))
        return 1;

    auto& em = engine.entityManager();

    // Emplace config structs in registry ctx before loading.
    em.registry().ctx().emplace<FormulaConfig>();
    em.registry().ctx().emplace<SoundConfig>();
    em.registry().ctx().emplace<MusicConfig>();
    em.registry().ctx().emplace<WaveConfig>();
    em.registry().ctx().emplace<WaveState>();
    em.registry().ctx().emplace<ItemRegistry>();
    em.registry().ctx().emplace<RecipeRegistry>();

    // Load balance formulas first -- all systems read from ctx<FormulaConfig>.
    ConfigLoader::loadFormulas(em, "config/balance/formulas.json");

    // Load item definitions -- must come before loadEntity so equipment loaders
    // can reference item defs.
    ConfigLoader::loadItemDefs(em, "config/items");

    // Load recipes -- must come after item defs so references are valid.
    ConfigLoader::loadRecipes(em, "config/recipes");

    // Load sound mappings -- all systems read from ctx<SoundConfig>.
    ConfigLoader::loadSounds(em, "config/audio/sounds.json");

    // Load music track list -- WaveSystem picks a random track per wave.
    ConfigLoader::loadMusic(em, "config/audio/music.json");

    // Load wave definitions -- WaveSystem reads from ctx<WaveConfig>.
    ConfigLoader::loadWaves(em, "config/waves.json");

    // Generate the tile map — populates em.tile_map / em.tile_config and
    // returns the world-space centre of the first placed room (player spawn).
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms");
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Spawn non-enemy entities from tile map markers.
    // 'R' -> rest spot. Enemy spawning is handled by WaveSystem.
    float restX = px;
    float restY = py;
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
        restX = sp.x;
        restY = sp.y;
    }

    // Player — placed at the bonfire (rest spot) so they start in the safe room.
    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = restX;
        t.y = restY;
    }

    if (em.registry().valid(player))
    {
        em.registry().emplace<PlayerActions>(player);
        em.registry().emplace<Wallet>(player);
        em.registry().emplace<InteractTarget>(player);
        const auto& pt = em.registry().get<Transform>(player);
        em.registry().emplace<Camera>(player, Camera{pt.x, pt.y, true});
    }

    // Derive Health.max from END stats for all stat-based entities.
    LevelingSystem::applyInitialDerivations(em);

    // Auto-start wave 1 (plays music, sets needs_map_regen which places
    // the player at the bonfire via handleMapRegen).
    WaveSystem::startNextWave(em);

    engine.setGameUpdate(&gameUpdate);
    engine.setPerFrameUpdate(&gamePerFrame);
    engine.run();
    return 0;
}
