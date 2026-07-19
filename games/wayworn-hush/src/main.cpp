#include "Arcs.h"
#include "Engine.h"
#include "FontManager.h"
#include "Footsteps.h"
#include "GameLoop.h"
#include "Glimmer.h"
#include "JsonConfig.h"
#include "LdtkImport.h"
#include "Notify.h"
#include "PausePage.h"
#include "PlayerMovement.h"
#include "SaveGame.h"
#include "ScreenStyle.h"
#include "ThoughtBox.h"
#include "TitleScreen.h"
#include "Version.h"
#include "WatchHud.h"
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
    screen_style::init(body, label); // the shared register every full-screen surface draws in
    pause_page::init(body);
    notify::init(label, gs.hud.notification);
    interaction_mode::init(label); // the stance badge uses the small label font
    watch_hud::init(label);        // the watch readout sits beside it, same font
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

// Put the player at (wx,wy) -- and the view of them with it. The one place a placement
// happens, so the map's spawn and a save's resume can't disagree about what that means.
//
// Every "previous" value moves too. The renderer interpolates between previous and current
// (both for the sprite and for the camera the player carries), so leaving them behind makes
// the first frames after a placement lerp from somewhere the player never was -- the world
// visibly slides into position instead of simply being there.
void placePlayer(EntityManager& em, const GameState& gs, float wx, float wy)
{
    auto& reg = em.registry();
    auto& t = reg.get<Transform>(gs.player);
    t.x = wx;
    t.y = wy;
    if (auto* pt = reg.try_get<PreviousTransform>(gs.player))
    {
        pt->x = wx;
        pt->y = wy;
    }
    if (auto* cam = reg.try_get<Camera>(gs.player))
    {
        cam->x = wx;
        cam->y = wy;
        cam->prev_x = wx;
        cam->prev_y = wy;
    }
}

// Anchor every entity's interpolation snapshot to where it actually is. The engine takes
// that snapshot at the TOP of a tick, so anything spawned during one (a whole world, built
// mid-update) is missed: its "previous" stays default-constructed at the origin and the
// renderer lerps it in from there -- the world assembles itself out of the corner for a
// frame instead of simply being there. Call once, after a world is built.
void anchorInterpolation(EntityManager& em)
{
    auto& reg = em.registry();
    for (auto [e, t] : reg.view<Transform>().each())
    {
        auto& prev = reg.get_or_emplace<PreviousTransform>(e);
        prev.x = t.x;
        prev.y = t.y;
    }
    for (auto [e, cam] : reg.view<Camera>().each())
    {
        cam.prev_x = cam.x;
        cam.prev_y = cam.y;
        cam.prev_offset_x = cam.offset_x;
        cam.prev_offset_y = cam.offset_y;
    }
}

// Rebuild the state that a WALK mutates back to how it's authored -- the starting point a
// pilgrim's saved walk is then applied onto.
//
// This is the reset. Reloading from config is what makes it structural: the fields config
// owns are rebuilt from the one source of truth, so nothing can be left dirty because
// someone forgot to add it to a clear-list. Only the two loaders whose output a walk
// changes are here -- observations (which holds the record of what's been seen/done) and
// growth (whose stat_levels a walk raises from their authored starting values). The rest
// (tables, feel, art paths) is read-only at runtime and loaded once at boot.
void resetAuthoredState(GameState& gs)
{
    observations::load(gs.observations, "config/observations.json", "config/actions.json");
    growth::load(gs.growth, "config/faculties.json");
    // Only the CADENCE is authored -- the loader leaves `seconds` alone, which is the walk's
    // own elapsed time and comes from the save (applied after this).
    worldclock::load(gs.clock, "config/world_clock.json");
}

// Bring a world into being and step into it -- the ONE path from the title into play,
// whether continuing a saved walk (`saved`) or starting one (nullopt). Builds the region,
// spawns the player + props, overlays any save, and starts the ambient bed. Returns false
// if the region couldn't load (fatal -- the region IS the map).
//
// Nothing here runs at boot: greeting a player must not require a loaded region, and a
// fresh "begin again" gets a world built from scratch rather than an old one reset (see
// AppState / docs/design/SHELL.md).
bool enterWorld(Engine& engine, EntityManager& em, GameState& gs, bool continue_saved);

// Load the authored LDtk region into the world (fatal if it fails -- the region IS the
// map). Applies terrain + surface map, uploads it, spawns the player at the region's
// PlayerSpawn, binds observation placements, and spawns glimmer signals + tile-carrying
// prop entities (trees/rocks). See docs/design/MAP-PIPELINE.md.
bool setupRegion(Engine& engine, EntityManager& em, GameState& gs)
{
    const world_config::Config& wc = gs.world_config;
    const ldtk::Region region =
        ldtk::load(wc.ldtk, wc.tileset_png, gs.surface_config, gs.structure_config);
    std::fprintf(stderr,
                 "[region] ldtk load %s: %dx%d, %zu objects, %zu props, %zu encounters, %zu "
                 "pickups\n",
                 region.ok ? "OK" : "FAILED", region.map.width, region.map.height,
                 region.objects.size(), region.props.size(), region.encounters.size(),
                 region.pickups.size());
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
            placePlayer(em, gs, o.wx, o.wy);

    // Bind observation PLACEMENTS from the map onto the loaded observation content: an
    // entity carrying an `encounter` field supplies its position/size/trigger. Content
    // (observations.json) and placement (LDtk) meet by id here -- before glimmer spawns
    // (it reads each encounter's world position). Authoring gaps are logged, not fatal.
    {
        std::vector<observations::Placement> placements;
        placements.reserve(region.encounters.size());
        for (const auto& p : region.encounters)
            placements.push_back({p.id, p.placement_id, p.x, p.y, p.w, p.h,
                                  observations::triggerFromString(p.trigger)});
        const auto rep = observations::applyPlacements(gs.observations, placements);
        for (const auto& id : rep.placements_without_encounter)
            std::fprintf(stderr, "[observe] placement '%s' has no observation content\n",
                         id.c_str());
        for (const auto& id : rep.encounters_without_placement)
            std::fprintf(stderr, "[observe] observation '%s' has no placement in the map\n",
                         id.c_str());
    }

    glimmer::spawn(em, gs.observations, gs.glimmer_config, gs.gone);
    world_items::spawn(em, region.pickups, gs.items, gs.loot_tables, gs.world_items_config,
                       gs.gone);
    ldtk::spawnProps(em, region, wc.tileset_png);
    return true;
}

bool enterWorld(Engine& engine, EntityManager& em, GameState& gs, const std::string& id)
{
    const savegame::File file = savegame::load();
    const savegame::Data* pilgrim = savegame::find(file, id);
    if (pilgrim == nullptr)
    {
        std::fprintf(stderr, "[app] no pilgrim '%s' to walk as\n", id.c_str());
        return false;
    }

    // Back to how the world is authored FIRST, so nothing of the last walk (a previous
    // pilgrim's stats, their record) survives into this one. apply() then overwrites the
    // rest with whatever this pilgrim has done. Between them, every field is accounted for
    // without a clear-list to keep in step.
    resetAuthoredState(gs);

    // The pilgrim's walk is applied BEFORE the world is built, because building it READS
    // that walk: the spawners skip whatever this pilgrim already took (gs.gone), and the
    // observation record decides what's visible. Build first and the world is made from an
    // empty record -- every taken thing back on the ground, to be taken again.
    savegame::apply(*pilgrim, gs);
    gs.app.active_id = pilgrim->id;

    // Terrain + player + props, filtered by the walk above. The region IS the map -- a
    // failed load is fatal.
    if (!setupRegion(engine, em, gs))
        return false;

    if (!pilgrim->place.walked)
    {
        // They have never set out: leave them at the map's spawn (NOT the saved place,
        // which is meaningless before a first step) and give them the notebook -- a key
        // item; carrying it is what lets thoughts be written down (docs/design/INVENTORY.md).
        inventory::add(gs.satchel, gs.items, inventory::ItemInstance{"notebook"});
        // BANDAID(approved): the watch is meant to be FOUND, not started with -- telling the
        // time is an earned capability (docs/design/INVENTORY.md, NOTEBOOK.md). Granted here
        // so the time-reading surfaces can be exercised before its world placement is
        // authored; remove the moment it exists as a pickup on the map.
        inventory::add(gs.satchel, gs.items, inventory::ItemInstance{"watch"});
    }
    else if (player_movement::canStand(em, gs.player, pilgrim->place.x, pilgrim->place.y))
    {
        placePlayer(em, gs, pilgrim->place.x, pilgrim->place.y);
    }
    else
    {
        // The spot is no longer somewhere they can BE: a walk saved against an older map
        // (or a hand-edited file) can name a place that is now water, inside a rock, or off
        // the world entirely. Falling back to the spawn keeps a stale walk playable instead
        // of stranding them in the void.
        std::fprintf(stderr,
                     "[save] resume point (%.0f,%.0f) is not standable -- "
                     "starting from the map's spawn instead\n",
                     static_cast<double>(pilgrim->place.x), static_cast<double>(pilgrim->place.y));
    }

    // The over-head thought bubble: one player-attached sprite that pops (faculty-hued)
    // while a thought reading is on screen. See HeadMarker / docs/design/HUD.md.
    head_marker::spawn(em, gs.head_marker_config);

    // The region's ambient bed. Loops with a slow fade-in so the world eases in rather
    // than snapping on. Low volume -- the score is sparse and unhurried (see
    // docs/design/AESTHETIC.md). Runs silent if no audio device.
    AudioSystem::playMusic(gs.world_config.ambient_track, gs.world_config.ambient_volume,
                           /*loop=*/true, gs.world_config.ambient_fade_in_ms);

    // Everything above was spawned mid-tick; anchor it before a frame renders it.
    anchorInterpolation(em);
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
    if (const auto aj = config::load("config/atmosphere.json"))
    {
        const auto& g = aj->value("grade", nlohmann::json::object());
        engine::gl::Grade grade;
        grade.saturation = g.value("saturation", 1.0f);
        grade.brightness = g.value("brightness", 1.0f);
        grade.tint_r = g.value("tint_r", 1.0f);
        grade.tint_g = g.value("tint_g", 1.0f);
        grade.tint_b = g.value("tint_b", 1.0f);
        engine::gl::pixelTargetSetGrade(grade);
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
    glimmer::load(gs.glimmer_config, "config/glimmer.json"); // before setupRegion (spawns glimmers)
    formulas::load(gs.formulas, "config/formulas.json");     // stat-driven formulas (glow, ...)
    world_items::load(gs.world_items_config, "config/world_items.json");        // floor item feel
    interaction_mode::load(gs.int_mode_config, "config/interaction_mode.json"); // stance badge
    watch_hud::load(gs.watch_hud_config, "config/watch_hud.json");              // watch readout
    world_config::load(gs.world_config, "config/world.json"); // region asset paths

    inventory::load(gs.items, "config/items");
    loot::load(gs.loot_tables, "config/loot");    // gather tables (rolled by ActionKind::Gather)
    crafting::load(gs.recipes, "config/recipes"); // recipes (combine -> made thing)
    crafting::loadConfig(gs.crafting_config, "config/crafting.json"); // outcome/XP tuning

    head_marker::load(gs.head_marker_config, "config/head_marker.json");

    // Story arcs are authoring apparatus, not a runtime system: nothing below reads them.
    // Loading them here checks the authored threads against what the content can actually
    // produce, so a renamed flag that strands a route is reported at boot rather than found
    // in play. Warnings only -- a broken arc never blocks the game.
    {
        arcs::Registry authored;
        arcs::load(authored, "config/arcs.json");
        for (const auto& p : arcs::validate(authored, arcs::survey(gs.observations)))
            std::fprintf(stderr, "[arc] '%s' %s\n", p.arc.c_str(), p.detail.c_str());
    }

    // UI fonts by ROLE on a 1.25 (Major-Third) scale, authored as canvas FRACTIONS
    // (config/fonts.json): body = reading text; label = headings / notifications /
    // captions. Fractions * hud::scale(window) give the pixel size, so text scales
    // with the window (reloadHudFonts / gameOnResize). The placeholder face is a
    // serif stand-in until the pixel-font aesthetic pass (docs/design/AESTHETIC.md).
    if (const auto fj = config::load("config/fonts.json"))
    {
        sFontCfg.face = fj->value("face", sFontCfg.face);
        sFontCfg.body_frac = fj->value("body", sFontCfg.body_frac);
        sFontCfg.label_frac = fj->value("label", sFontCfg.label_frac);
    }
    if (const auto bj = config::load("config/observation_box.json"))
    {
        sBoxCfg.blip_sound = bj->value("blip_sound", sBoxCfg.blip_sound);
        sBoxCfg.appear_sound = bj->value("appear_sound", sBoxCfg.appear_sound);
        sBoxCfg.notebook_sound = bj->value("notebook_sound", sBoxCfg.notebook_sound);
        sBoxCfg.drop_in_secs = bj->value("drop_in_secs", sBoxCfg.drop_in_secs);
        // chars_per_sec=0 would freeze the typewriter -> the reading never completes and the
        // box can't be dismissed (soft-lock). Floor it. blip_every=0 would blip every
        // character (SFX spam); floor to 1.
        sBoxCfg.chars_per_sec = std::max(1.0f, bj->value("chars_per_sec", sBoxCfg.chars_per_sec));
        sBoxCfg.fade_out_secs = bj->value("fade_out_secs", sBoxCfg.fade_out_secs);
        sBoxCfg.blip_every = std::max(1, bj->value("blip_every", sBoxCfg.blip_every));
    }
    // The fixed HUD region rects (canvas fractions -- see HudCanvas / config/hud.json).
    // GameState owns them (one source of truth); the thought box and notification channel
    // render into these bands. reloadHudFonts loads the role fonts at the current size and
    // points the HUD systems at them.
    hud::loadRegions(gs.hud, "config/hud.json");
    // How the player likes the HUD. Config authors the DEFAULT; a save then carries what
    // they actually chose. Read in that order and merged field-by-field (decodeSettings
    // falls back to what it is handed), so a setting the player has never touched keeps the
    // authored default rather than a struct's zero. The render loop reads gs.prefs.
    gs.prefs.hud.visibility = hud::loadVisibility("config/hud.json", gs.prefs.hud.visibility);
    gs.prefs = savegame::loadSettings(gs.prefs);
    reloadHudFonts(gs, engine.windowWidth(), engine.windowHeight());

    // The world (and its ambient bed) is built when the player commits from the title --
    // see enterWorld. Boot ends at the greeting.
    //
    // Whether there's a walk to continue is probed ONCE here: the title only needs to know
    // if the entry exists, and asking the disk every frame to draw a menu is absurd.
    gs.app.pilgrim_count = static_cast<int>(savegame::load().pilgrims.size());
    setWorldEnter(&enterWorld);
    title_screen::reset();

    engine.setGameUpdate(&gameUpdate);
    engine.setPreRender(&gamePreRender);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setRenderUI(&gameRenderUI);
    engine.setRenderImGui(&gameRenderImGui);
    engine.run();

    // Keep the walk on the way out. Quitting from the page already wrote; this covers
    // every other exit (the window's close button, Alt+F4) so leaving never costs it.
    // saveNow no-ops when nobody is walking, so leaving from the title can't overwrite a
    // real walk with the empty state of a game that was never played.
    if (gs.app.world_built)
        saveNow(em, gs);

    RenderSystem::shutdown();
    TileMapRenderer::shutdown();
    engine::gl::pixelTargetShutdown();
    return 0;
}
