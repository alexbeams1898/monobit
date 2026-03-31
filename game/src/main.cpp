#include "ConfigLoader.h"
#include "Engine.h"
#include "FontManager.h"
#include "GameLoop.h"
#include "SaveManager.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "renderers/AIRecorder.h"
#include "renderers/DebugOverlay.h"
#include "renderers/HudRenderer.h"
#include "renderers/InteractionPromptRenderer.h"
#include "renderers/ItemStatRenderer.h"
#include "screens/CharCreateScreen.h"
#include "screens/CraftingScreen.h"
#include "screens/GameOverScreen.h"
#include "screens/HighScoresScreen.h"
#include "screens/LevelUpScreen.h"
#include "screens/LoadGameScreen.h"
#include "screens/MainMenuScreen.h"
#include "screens/PauseMenu.h"
#include "screens/RunSummaryScreen.h"
#include "screens/SanctuaryScreen.h"
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
    em.registry().ctx().emplace<WeaponTierRegistry>();
    em.registry().ctx().emplace<EvolutionRegistry>();
    em.registry().ctx().emplace<Compendium>();
    em.registry().ctx().emplace<UIState>();
    em.registry().ctx().emplace<GameState>();
    em.registry().ctx().emplace<RunStats>();
    em.registry().ctx().emplace<ScoringConfig>();
    em.registry().ctx().emplace<SaveData>();
    em.registry().ctx().emplace<AttackTokenPool>();

    // Load configs.
    ConfigLoader::loadFormulas(em, "config/balance/formulas.json");
    em.registry().ctx().get<AttackTokenPool>().max_tokens =
        em.registry().ctx().get<FormulaConfig>().combat_ai.max_attack_tokens;
    ConfigLoader::loadItemDefs(em, "config/items");
    ConfigLoader::loadRecipes(em, "config/recipes");
    ConfigLoader::loadWeaponTiers(em, "config/balance/weapon_tiers.json");
    ConfigLoader::loadEvolutionTrees(em, "config/evolution");
    ConfigLoader::loadSounds(em, "config/audio/sounds.json");
    ConfigLoader::loadMusic(em, "config/audio/music.json");
    ConfigLoader::loadWaves(em, "config/waves.json");
    ConfigLoader::loadScoring(em, "config/balance/scoring.json");

    // Load saved data (characters, high scores).
    em.registry().ctx().get<SaveData>() = SaveManager::load();

    // Start at main menu -- world is created when the player selects a character.
    em.registry().ctx().get<GameState>().phase = GameState::Phase::MainMenu;

    // Play main menu music (random chance of rare reversed variant).
    playMainMenuMusic(em);

    // Load fonts and init all UI screens.
    const FontHandle bodyFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 28.0f);
    const FontHandle titleFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 36.0f);
    const FontHandle bigTitleFont = FontManager::loadFont("assets/fonts/cinzel.ttf", 72.0f);
    gameLoopInit(titleFont);
    HudRenderer::init(bodyFont, titleFont, &engine.textureManager());
    NotificationSystem::init(bodyFont, titleFont);
    InteractionPromptRenderer::init(bodyFont);
    DebugOverlay::init(bodyFont);
    AIRecorder::init();
    ItemStatRenderer::init(&engine.textureManager());
    PauseMenu::init(bodyFont, titleFont, &engine.textureManager());
    LevelUpScreen::init(bodyFont, titleFont);
    MainMenuScreen::init(bodyFont, titleFont, bigTitleFont);
    CharCreateScreen::init(bodyFont, titleFont);
    LoadGameScreen::init(bodyFont, titleFont, bigTitleFont);
    GameOverScreen::init(bodyFont, titleFont, bigTitleFont);
    VictoryScreen::init(bodyFont, titleFont, bigTitleFont);
    RunSummaryScreen::init(bodyFont, titleFont);
    HighScoresScreen::init(bodyFont, titleFont);
    SanctuaryScreen::init(bodyFont, titleFont);
    CraftingScreen::init(bodyFont, titleFont);

    engine.setGameUpdate(&gameUpdate);
    engine.setPerFrameUpdate(&gamePerFrame);
    engine.setRenderDebug(&gameRenderDebug);
    engine.setRenderUI(&gameRenderUI);
    engine.run();
    return 0;
}
