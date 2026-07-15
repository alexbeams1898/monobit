#include "Engine.h"
#include "FontManager.h"
#include "Footsteps.h"
#include "GameLoop.h"
#include "Glimmer.h"
#include "LdtkImport.h"
#include "Notify.h"
#include "PausePage.h"
#include "ThoughtBox.h"
#include "Version.h"
#include "WorldInit.h"
#include "ecs/Components.h"
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
#include <filesystem>
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

// LDtk trigger-field string -> the observation Trigger enum (empty/unknown = Observe).
// Values match the LDtk Trigger enum exactly (LDtk capitalizes enum ids).
observations::Trigger toTrigger(const std::string& s)
{
    if (s == "Enter")
        return observations::Trigger::Enter;
    return observations::Trigger::Observe;
}

// Load the authored LDtk region (real tileset art) into the world, or fall back to the
// flat-color placeholder. Applies terrain + surface map, uploads it, spawns the player
// at the region's PlayerSpawn (if any), and spawns glimmer signals + tile-carrying prop
// entities (trees/rocks -- Y-sorted, front/behind the player by base). Observation
// placements (where each observable lives) are bound from the map here. See
// docs/design/MAP-PIPELINE.md.
bool setupRegion(Engine& engine, EntityManager& em, GameState& gs)
{
    const ldtk::Region region =
        ldtk::load("assets/tilesets/source/overworld.ldtk", "assets/tilesets/overworld.png",
                   gs.surface_config, gs.structure_config);
    std::fprintf(stderr, "[region] ldtk load %s: %dx%d, %zu objects, %zu props, %zu observables\n",
                 region.ok ? "OK" : "FAILED", region.map.width, region.map.height,
                 region.objects.size(), region.props.size(), region.observables.size());
    if (!region.ok)
    {
        // The LDtk region IS the map -- no fallback. A failed load (missing/corrupt file)
        // is fatal: the caller aborts rather than launch into an empty world.
        std::fprintf(stderr, "[region] FATAL: could not load the region; aborting.\n");
        return false;
    }
    em.tile_map = region.map;
    em.tile_config = region.config;
    gs.tile_surface = region.tile_surface; // tile id -> surface, for footsteps
    gs.cell_surface = region.cell_surface; // per-cell override (bridge decks, etc.)
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    gs.player = world_init::spawnPlayer(em, gs.player_config);
    for (const auto& o : region.objects)
        if (o.type == "PlayerSpawn")
        {
            auto& t = em.registry().get<Transform>(gs.player);
            t.x = o.wx;
            t.y = o.wy;
            if (auto* pt = em.registry().try_get<PreviousTransform>(gs.player))
            {
                pt->x = o.wx;
                pt->y = o.wy;
            }
        }

    // Bind observation PLACEMENTS from the map onto the loaded observation content: an
    // entity carrying an `observable` field supplies its position/size/trigger. Content
    // (observations.json) and placement (LDtk) meet by id here -- before glimmer spawns
    // (it reads each observable's world position). Authoring gaps are logged, not fatal.
    {
        std::vector<observations::Placement> placements;
        placements.reserve(region.observables.size());
        for (const auto& p : region.observables)
            placements.push_back({p.id, p.x, p.y, p.w, p.h, toTrigger(p.trigger)});
        const auto rep = observations::applyPlacements(gs.observations, placements);
        for (const auto& id : rep.placements_without_observable)
            std::fprintf(stderr, "[observe] placement '%s' has no observation content\n",
                         id.c_str());
        for (const auto& id : rep.observables_without_placement)
            std::fprintf(stderr, "[observe] observation '%s' has no placement in the map\n",
                         id.c_str());
    }

    glimmer::spawn(em, gs.observations);
    ldtk::spawnProps(em, region, "assets/tilesets/overworld.png");
    return true;
}
} // namespace

// Reopen stdio onto wayworn-hush.log (next to the exe, truncated per run) so
// fprintf/cerr diagnostics always land somewhere readable regardless of how the
// game was launched (F7 debugger, double-click, headless). Line-buffered so
// progress appears as it happens. Mirrors selva-oscura's redirect.
static void redirectStdioToLog()
{
    (void)std::freopen("wayworn-hush.log", "w", stdout);
    (void)std::freopen("wayworn-hush.log", "a", stderr);
    std::setvbuf(stderr, nullptr, _IOLBF, 4096);
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // Dev builds: run from the SOURCE tree so assets/config are read directly (one
    // source of truth -- an LDtk/JSON/PNG edit is live next launch, no copy step to
    // lag or lock). WAYWORN_SOURCE_DIR is defined by CMake for non-release builds
    // only; release builds skip this and read assets shipped beside the exe.
#ifdef WAYWORN_SOURCE_DIR
    {
        std::error_code ec;
        std::filesystem::current_path(WAYWORN_SOURCE_DIR, ec); // no-op on failure
    }
#endif

    redirectStdioToLog();

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
    surfaces::load(gs.surface_config, "config/surfaces.json");
    structures::load(gs.structure_config, "config/structures.json");

    // Item blueprints, then the pilgrim's starting satchel: he sets out carrying his
    // notebook (a key item -- carrying it is what lets thoughts be written down; see
    // docs/design/INVENTORY.md). The watch is found later, not started with.
    inventory::load(gs.items, "config/items");
    inventory::add(gs.satchel, gs.items, inventory::ItemInstance{"notebook"});

    // Terrain + player + props: load the authored LDtk region, apply it, and spawn the
    // player at its PlayerSpawn. The region IS the map -- a failed load is fatal.
    if (!setupRegion(engine, em, gs))
        return 1;

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
            // chars_per_sec=0 would freeze the typewriter -> the reading never completes
            // and the box can't be dismissed (soft-lock). Floor it. blip_every=0 would
            // blip every character (SFX spam); floor to 1.
            sBoxCfg.chars_per_sec =
                std::max(1.0f, bj.value("chars_per_sec", sBoxCfg.chars_per_sec));
            sBoxCfg.fade_out_secs = bj.value("fade_out_secs", sBoxCfg.fade_out_secs);
            sBoxCfg.blip_every = std::max(1, bj.value("blip_every", sBoxCfg.blip_every));
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
