#include "ConfigLoader.h"
#include "Engine.h"
#include "FontManager.h"
#include "GameLoop.h"
#include "SaveManager.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "renderers/DebugOverlay.h"
#include "renderers/HudRenderer.h"
#include "renderers/InteractionPromptRenderer.h"
#include "screens/CharCreateScreen.h"
#include "screens/GameOverScreen.h"
#include "screens/HighScoresScreen.h"
#include "screens/LevelUpScreen.h"
#include "screens/LoadGameScreen.h"
#include "screens/MainMenuScreen.h"
#include "screens/PauseMenu.h"
#include "screens/RunSummaryScreen.h"
#include "screens/VictoryScreen.h"
#include "systems/AudioSystem.h"
#include "systems/NotificationSystem.h"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>
// ---------------------------------------------------------------------------
// Crash reporter
// ---------------------------------------------------------------------------

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
    em.registry().ctx().emplace<UIState>();
    em.registry().ctx().emplace<GameState>();
    em.registry().ctx().emplace<RunStats>();
    em.registry().ctx().emplace<ScoringConfig>();
    em.registry().ctx().emplace<SaveData>();

    // Load configs.
    ConfigLoader::loadFormulas(em, "config/balance/formulas.json");
    ConfigLoader::loadItemDefs(em, "config/items");
    ConfigLoader::loadRecipes(em, "config/recipes");
    ConfigLoader::loadSounds(em, "config/audio/sounds.json");
    ConfigLoader::loadMusic(em, "config/audio/music.json");
    ConfigLoader::loadWaves(em, "config/waves.json");
    ConfigLoader::loadScoring(em, "config/balance/scoring.json");

    // Load saved data (characters, high scores).
    em.registry().ctx().get<SaveData>() = SaveManager::load();

    // Start at main menu -- world is created when the player selects a character.
    em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;

    // Play main menu music.
    if (auto* t = em.registry().ctx().get<MusicConfig>().get("main_menu"))
        AudioSystem::playMusic(t->path, t->volume);

    // Load fonts and init all UI screens.
    FontHandle bodyFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 28.0f);
    FontHandle titleFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 36.0f);
    FontHandle bigTitleFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 72.0f);
    HudRenderer::init(bodyFont, titleFont);
    NotificationSystem::init(bodyFont, titleFont);
    InteractionPromptRenderer::init(bodyFont);
    DebugOverlay::init(bodyFont);
    PauseMenu::init(bodyFont, titleFont, &engine.textureManager());
    LevelUpScreen::init(bodyFont, titleFont);
    MainMenuScreen::init(bodyFont, titleFont, bigTitleFont);
    CharCreateScreen::init(bodyFont, titleFont);
    LoadGameScreen::init(bodyFont, titleFont, bigTitleFont);
    GameOverScreen::init(bodyFont, titleFont, bigTitleFont);
    VictoryScreen::init(bodyFont, titleFont, bigTitleFont);
    RunSummaryScreen::init(bodyFont, titleFont);
    HighScoresScreen::init(bodyFont, titleFont);

    engine.setGameUpdate(&gameUpdate);
    engine.setPerFrameUpdate(&gamePerFrame);
    engine.setRenderUI(&gameRenderUI);
    engine.run();
    return 0;
}
