#include "Engine.h"
#include "FontManager.h"
#include "Footsteps.h"
#include "GameLoop.h"
#include "Glimmer.h"
#include "Notify.h"
#include "PausePage.h"
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

// ---------------------------------------------------------------------------
// HUD fonts scale with the window: sizes are canvas fractions (config/fonts.json),
// resolved to pixels via hud::scale(window) each time the window resizes. The
// atlas reload is cheap (FontManager caches by face+size), and re-init just swaps
// handles into the HUD systems. This keeps text proportional to the HUD regions
// at any resolution instead of a fixed pixel size that overflows small windows.
// ---------------------------------------------------------------------------
namespace
{
struct HudFontConfig
{
    std::string face = "assets/fonts/placeholder.ttf";
    float body_frac = 0.01875f;  // reading text
    float label_frac = 0.01211f; // headings / notifications / captions
};
HudFontConfig sFontCfg;
thought_box::Config sBoxCfg; // feel + SFX, loaded once; re-passed on every resize

// (Re)load the role fonts at the current window size and point the HUD systems at
// the fresh handles. Called once at startup and from the resize callback.
void reloadHudFonts(GameState& gs, int windowW, int windowH)
{
    const float s = hud::scale(windowW, windowH);
    const FontHandle body = FontManager::loadFont(sFontCfg.face, sFontCfg.body_frac * s);
    const FontHandle label = FontManager::loadFont(sFontCfg.face, sFontCfg.label_frac * s);
    thought_box::init(body, label, sBoxCfg, gs.hud);
    pause_page::init(body);
    notify::init(label, gs.hud.notification);
}

// Engine resize callback: keep the pixel target + HUD fonts in step with the new
// window size. GameState (with the loaded HUD regions + font config already set)
// is reached via the registry context.
void gameOnResize(Engine& engine, int w, int h)
{
    engine::gl::pixelTargetResize(w, h);
    auto& gs = engine.entityManager().registry().ctx().get<GameState>();
    reloadHudFonts(gs, w, h);
}
} // namespace

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

    // Launch borderless-fullscreen at the desktop's native resolution (the common
    // default). The world renders 16:9 + integer-upscales; the HUD is authored on a
    // 16:9 reference canvas (HudCanvas) so it's correct at any resolution. The
    // 1536x864 passed here is the windowed restore size (2x the 768x432 internal).
    // (Windowed / fullscreen becomes a player Setting in a later slice.)
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);
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
    // gameOnResize keeps the pixel target AND the HUD fonts in step with the window
    // (registered after GameState exists so the callback can reach gs.hud).
    engine.setOnResize(&gameOnResize);

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

    GameState& gs = em.registry().ctx().emplace<GameState>();
    gs.player_config = loadPlayerConfig("config/player.json");
    observations::load(gs.observations, "config/observations.json", "config/actions.json");
    growth::load(gs.growth, "config/faculties.json");
    footsteps::load(gs.footstep_config, "config/footsteps.json");

    // Build base terrain, then stamp observable tiles FROM the loaded config (one
    // source of truth), then upload -- so every authored observable is visible.
    world_init::buildPlaceholderRegion(em);
    world_init::placeObservableTiles(em, gs.observations);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    gs.player = world_init::spawnPlayer(em, gs.player_config);
    glimmer::spawn(em, gs.observations);

    // The over-head thought bubble: one player-attached sprite that pops (faculty-
    // hued) while a thought reading is on screen. See HeadMarker / docs/design/HUD.md.
    head_marker::load(gs.head_marker_config, "config/head_marker.json");
    head_marker::spawn(em, gs.head_marker_config);

    // UI fonts by ROLE on a 1.25 (Major-Third) scale, authored as canvas FRACTIONS
    // (config/fonts.json): body = reading text; label = headings / notifications /
    // captions. Fractions * hud::scale(window) give the pixel size, so text scales
    // with the window (reloadHudFonts / gameOnResize). The placeholder face is a
    // serif stand-in until the pixel-font aesthetic pass (docs/design/AESTHETIC.md).
    if (std::ifstream ff{"config/fonts.json"})
    {
        const auto fj = nlohmann::json::parse(ff, nullptr, false);
        if (!fj.is_discarded())
        {
            sFontCfg.face = fj.value("face", sFontCfg.face);
            sFontCfg.body_frac = fj.value("body", sFontCfg.body_frac);
            sFontCfg.label_frac = fj.value("label", sFontCfg.label_frac);
        }
    }
    if (std::ifstream bf{"config/observation_box.json"})
    {
        const auto bj = nlohmann::json::parse(bf, nullptr, false);
        if (!bj.is_discarded())
        {
            sBoxCfg.blip_sound = bj.value("blip_sound", sBoxCfg.blip_sound);
            sBoxCfg.appear_sound = bj.value("appear_sound", sBoxCfg.appear_sound);
            sBoxCfg.notebook_sound = bj.value("notebook_sound", sBoxCfg.notebook_sound);
            sBoxCfg.drop_in_secs = bj.value("drop_in_secs", sBoxCfg.drop_in_secs);
            sBoxCfg.chars_per_sec = bj.value("chars_per_sec", sBoxCfg.chars_per_sec);
            sBoxCfg.fade_out_secs = bj.value("fade_out_secs", sBoxCfg.fade_out_secs);
            sBoxCfg.blip_every = bj.value("blip_every", sBoxCfg.blip_every);
        }
    }
    // The fixed HUD region rects + visibility mode (canvas fractions -- see
    // HudCanvas / config/hud.json). GameState owns them (one source of truth); the
    // thought box and notification channel render into these bands, and the render
    // loop reads gs.hud.visibility to gate HUD drawing. reloadHudFonts loads the
    // role fonts at the current size and points the HUD systems at them.
    hud::loadRegions(gs.hud, "config/hud.json");
    reloadHudFonts(gs, engine.windowWidth(), engine.windowHeight());

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
    engine.setRenderImGui(&gameRenderImGui);
    engine.run();

    RenderSystem::shutdown();
    TileMapRenderer::shutdown();
    engine::gl::pixelTargetShutdown();
    return 0;
}
