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
#include "SpiritHud.h"
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
    tutorial::init(body, label);     // teaching cards share the box's role fonts
    screen_style::init(body, label); // the shared register every full-screen surface draws in
    pause_page::init(body);
    notify::init(label, gs.hud.notification);
    interaction_mode::init(label); // the stance badge uses the small label font
    watch_hud::init(label);        // the watch readout sits beside it, same font
    spirit_hud::init(label);       // and the Spirit total, opposite corner, same font
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
        // Snap THROUGH the placement rule (bounds clamp / small-map centering), then
        // anchor prev to the clamped spot -- otherwise the first frame interpolates
        // from the raw player position toward wherever the tick's clamp puts it.
        clampCameraToMap(em, gs);
        cam->prev_x = cam->x;
        cam->prev_y = cam->y;
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
// changes are here -- psyche (which holds the record of what's been seen/done) and
// growth (whose stat_levels a walk raises from their authored starting values). The rest
// (tables, feel, art paths) is read-only at runtime and loaded once at boot.
void resetAuthoredState(GameState& gs)
{
    psyche::load(gs.psyche, "config/psyche.json", "config/actions.json");
    // What a flag landing is WORTH, derived from what it OPENS rather than authored: one
    // something gates on opened a way; a thread's goal closed the thread (see
    // RollConfig::exp_flag_read). Derived HERE, because the load above rebuilds State from
    // scratch -- deriving it once at boot would leave every actual walk with empty sets and
    // every flag silently worthless.
    gs.psyche.read_flags = arcs::survey(gs.psyche).read_flags;
    gs.psyche.goal_flags.clear();
    for (const auto& a : gs.threads.arcs)
        if (!a.goal_flag.empty())
            gs.psyche.goal_flags.insert(a.goal_flag);
    // Bind speakers: content authors WHO talks as an npc id; the display name is
    // authored once, in that character's config. Unknown speakers read under their
    // raw id -- visible in play, loud in the log, never silently mute.
    for (auto& o : gs.psyche.encounters)
    {
        if (o.speaker.empty())
            continue;
        const auto it = gs.npcs.npcs.find(o.speaker);
        if (it == gs.npcs.npcs.end())
        {
            std::fprintf(stderr, "[npc] observation '%s' speaker '%s' has no config/npcs entry\n",
                         o.id.c_str(), o.speaker.c_str());
            o.speaker_name = o.speaker;
        }
        else
        {
            o.speaker_name = it->second.name;
        }
    }
    // Remarks in an npc's voice resolve the same way ("player" resolves at push
    // time -- the pilgrim's name isn't known until the save is applied).
    for (auto& t : gs.psyche.thoughts)
    {
        if (t.voice.empty() || t.voice == "player")
            continue;
        const auto it = gs.npcs.npcs.find(t.voice);
        if (it == gs.npcs.npcs.end())
        {
            std::fprintf(stderr, "[npc] remark '%s' voice '%s' has no config/npcs entry\n",
                         t.id.c_str(), t.voice.c_str());
            t.voice_name = t.voice;
        }
        else
        {
            t.voice_name = it->second.name;
        }
    }
    growth::load(gs.growth, "config/faculties.json");
    // Only the CADENCE is authored -- the loader leaves `seconds` alone, which is the walk's
    // own elapsed time and comes from the save (applied after this).
    worldclock::load(gs.clock, "config/world_clock.json");
}

// Seed EVERY edge detector from the RESTORED world, in one place, because they all answer the
// same question and get it wrong the same way: a pump that compares against a
// default-constructed baseline reads the whole restored walk as having just happened. The
// symptoms look unrelated -- a TV clunking at boot, a notebook-want line at the title screen,
// thoughts re-rolling on load -- but they are one bug, and this is its one fix.
//
// AN ARRIVAL IS NOT AN EVENT. A new pump adds its baseline to GameState and its seeding here,
// in the same change.
void seedEdgeDetectors(GameState& gs)
{
    // The stat-change fingerprint: the pump must see only growth that happens in this walk,
    // never the restore itself (which would re-roll eligible thoughts and surface miss lines
    // nobody earned).
    gs.stats_seen_sum = growth::levelSum(gs.growth);
    // The unlock pump's baseline: fresh per walk, rebuilt from this pilgrim's restored
    // observation record by the first pump.
    gs.seeded_spots.clear();
    // The hour the clock was last re-checked for -- unseeded, the first tick reads as an hour
    // having just passed, and re-offers every stat-keyed thought.
    gs.clock_hour_seen = static_cast<int>(worldclock::fractionOfDay(gs.clock) * 24.0);
    // What he carries and holds. Unseeded, the first mirror reads as everything he owns having
    // just arrived in his hands.
    gs.psyche.carrying.clear();
    for (const auto& e : gs.satchel.items)
        gs.psyche.carrying.insert(e.id);
    gs.psyche.holding = gs.satchel.held;
    // The Spirit counter opens at his real total rather than climbing to it from whatever the
    // last walk left on screen -- the same rule, for the same reason.
    spirit_hud::reset(gs.growth.spirit_exp);
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

// Face the player along a spawn's authored cardinal.
void faceSpawn(EntityManager& em, const GameState& gs, const std::string& facing)
{
    auto* f = em.registry().try_get<FacingDirection>(gs.player);
    if (f == nullptr)
        return;
    float dx = 0.0f;
    float dy = 0.0f;
    ldtk::facingVec(facing, dx, dy);
    f->dx = dx;
    f->dy = dy;
    f->render_dx = dx;
    f->render_dy = dy;
}

// Apply a LOADED region to the world: terrain + surface map uploaded, player spawned at
// the arrival point named `spawn_id` (the default spawn when empty), observation
// placements bound, glimmers + floor items + props spawned. The load/apply split exists
// so a mid-walk switch can load the target level FIRST and only tear the world down once
// it is known good (see switchRegion).
// Where the player appears in a freshly applied region. Resolution order: a Warp id
// (warps are both ends of a passage -- you emerge at the named warp's center, stepping
// out along its facing), then a SpawnPoint id, then the auto-pair (below), then the
// level's default id-less spawn. One name, one order.
struct Arrival
{
    float x = 0.0f;
    float y = 0.0f;
    std::string facing;
    std::string label; // for the not-standable diagnostic
    bool found = false;
    // Half-extent of the arrival's own box along each axis (a warp's; 0 for spawns).
    // An arrival at a warp must CLEAR its box along `facing` before it can settle --
    // a door warp's center is inside the doorway (behind the building's face), so
    // emerging there leaves the player hidden behind the sprite it belongs to.
    float clear_w = 0.0f;
    float clear_h = 0.0f;
};

// WHERE YOU COME OUT. The rule: a warp names the warp it arrives at (`target`),
// and you emerge there, stepping clear of its box along its facing. Naming it is
// what makes a door provable -- with two doors between the same pair of levels,
// anything else is a coin flip, and a door that lands you in the wrong room is
// worse than one that says it is broken.
//
// A door that names nothing falls back to the return warp (see above), so a
// single-door room needs no authoring; the linter errors the moment that becomes
// ambiguous. `arrival_id` empty = nobody came through a
// door: a fresh walk, which starts at the map's id-less PlayerSpawn.
Arrival resolveArrival(const ldtk::Region& region, const std::string& arrival_id)
{
    if (!arrival_id.empty())
    {
        for (const auto& w : region.warps)
            if (w.id == arrival_id)
                return {w.x, w.y, w.facing, w.id, true, w.w * 0.5f, w.h * 0.5f};
        for (const auto& s : region.spawns) // a named spawn still works as a target
            if (s.id == arrival_id)
                return {s.wx, s.wy, s.facing, s.id, true};
        std::fprintf(stderr,
                     "[region] warp targets '%s', which is not a warp or spawn in '%s' -- "
                     "arriving at the level's spawn instead\n",
                     arrival_id.c_str(), region.level_id.c_str());
    }
    // The level's default (id-less) spawn -- where a NEW walk begins.
    for (const auto& s : region.spawns)
        if (s.id.empty())
            return {s.wx, s.wy, s.facing, s.id, true};
    if (!region.spawns.empty())
    {
        const ldtk::SpawnPoint& s = region.spawns.front();
        return {s.wx, s.wy, s.facing, s.id, true};
    }
    return {};
}

void applyRegion(Engine& engine, EntityManager& em, GameState& gs, const ldtk::Region& region,
                 const std::string& arrival_id)
{
    const world_config::Config& wc = gs.world_config;
    em.tile_map = region.map;
    em.tile_config = region.config;
    gs.tile_surface = region.tile_surface; // tile id -> surface, for footsteps
    gs.cell_surface = region.cell_surface; // per-cell override (bridge decks, etc.)
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Where the pilgrim now is + how to leave it. The warp latch starts DISARMED: the
    // arrival may sit inside the destination's own warp box, and it must not fire
    // until the player has first stepped clear of every box.
    gs.region = region.level_id;
    gs.region_warps = region.warps;
    gs.region_spawns = region.spawns;
    gs.region_interior = region.interior;
    gs.warp_armed = false;
    gs.pending_warp = {};
    // Any running scene's bodies died with the last region's registry; a scene
    // never survives a region switch (movement holds while one runs anyway).
    gs.scene_rt = {};
    // The old room's sounds don't follow you through a door.
    ambience::stopAll(gs.ambience_state, gs.ambience_config);

    gs.player = world_init::spawnPlayer(em, gs.player_config);
    Arrival at = resolveArrival(region, arrival_id);
    if (at.found)
    {
        // Step out along the arrival's facing: FIRST clear the arrival's own box (a door
        // warp's center is inside the doorway, behind the building's face -- settling
        // there leaves the player hidden behind the sprite), THEN keep stepping while the
        // spot is unstandable. A spawn (clear extents 0) that is already standable does
        // not move.
        {
            float dx = 0.0f;
            float dy = 0.0f;
            ldtk::facingVec(at.facing, dx, dy);
            const auto* col = em.registry().try_get<Collider>(gs.player);
            const float bodyHalf = col ? 0.5f * (dx != 0.0f ? col->width : col->height) : 8.0f;
            const float clear = std::abs(dx) * at.clear_w + std::abs(dy) * at.clear_h;
            if (clear > 0.0f)
            {
                at.x += dx * (clear + bodyHalf + 2.0f);
                at.y += dy * (clear + bodyHalf + 2.0f);
            }
            constexpr float kStepPx = 4.0f;
            constexpr int kMaxSteps = 32; // up to 4 further tiles of step-out
            for (int i = 0; i < kMaxSteps; ++i)
            {
                if (player_movement::canStand(em, gs.player, at.x, at.y))
                    break;
                at.x += dx * kStepPx;
                at.y += dy * kStepPx;
            }
        }
        std::fprintf(stderr, "[region] arrive '%s' at (%.0f,%.0f) in '%s'\n", at.label.c_str(),
                     static_cast<double>(at.x), static_cast<double>(at.y), region.level_id.c_str());
        placePlayer(em, gs, at.x, at.y);
        faceSpawn(em, gs, at.facing);
        // An unstandable arrival pins the player inside collision forever (out-of-bounds
        // counts as solid). That's an authoring slip -- say so loudly instead of letting
        // it present as "the game froze".
        if (!player_movement::canStand(em, gs.player, at.x, at.y))
            std::fprintf(stderr,
                         "[region] arrival '%s' at (%.0f,%.0f) in '%s' is NOT standable -- "
                         "the player will be stuck; move it in the editor\n",
                         at.label.c_str(), static_cast<double>(at.x), static_cast<double>(at.y),
                         region.level_id.c_str());
    }

    // Bind observation PLACEMENTS from the map onto the loaded observation content: an
    // entity carrying an `encounter` field supplies its position/size/trigger. Content
    // (psyche.json) and placement (LDtk) meet by id here -- before glimmer spawns
    // (it reads each encounter's world position). A placement naming unknown content is
    // an authoring gap and logged; content with no placement HERE is simply elsewhere in
    // the world (other levels hold it), so it is not reported.
    {
        std::vector<psyche::Placement> placements;
        placements.reserve(region.encounters.size());
        for (const auto& p : region.encounters)
            placements.push_back(
                {p.id, p.placement_id, p.x, p.y, p.w, p.h, psyche::triggerFromString(p.trigger)});
        const auto rep = psyche::applyPlacements(gs.psyche, placements);
        for (const auto& id : rep.placements_without_encounter)
            std::fprintf(stderr, "[observe] placement '%s' has no observation content\n",
                         id.c_str());
    }

    world_items::spawn(em, region.pickups, gs.items, gs.yield_tables, gs.world_items_config,
                       gs.gone);
    ldtk::spawnProps(em, region);

    // What the AUTHORED map says each clearing is made of -- counted from the placements, not
    // from what happens to be spawned, so a pilgrim resuming mid-work still knows how many
    // piles there were. `gone` supplies the other half: which of them are already hauled off.
    gs.region_clearings.clear();
    for (const auto& p : region.pickups)
    {
        if (p.group.empty())
            continue;
        GameState::Clearing& c = gs.region_clearings[p.group];
        c.placements.push_back(p.placement_id);
        if (!p.clears_flag.empty())
            c.flag = p.clears_flag;
    }

    // The people standing in this level. A placement naming an unknown character is
    // an authoring slip -- loud, not silent, or the kitchen is just mysteriously empty.
    // Bodies are recorded by npc id so scenes can steer the placed people too.
    gs.region_npcs.clear();
    gs.npc_routines.clear(); // routine progress belongs to the bodies just despawned
    for (const auto& n : region.npcs)
    {
        const auto it = gs.npcs.npcs.find(n.npc);
        if (it == gs.npcs.npcs.end())
            std::fprintf(stderr, "[npc] no character '%s' (config/npcs) for a placement in '%s'\n",
                         n.npc.c_str(), region.level_id.c_str());
        else
            gs.region_npcs[n.npc] = npc::spawn(em, it->second, n.wx, n.wy, n.facing);
    }

    // LAST, after every drawn thing exists: an encounter's highlight adopts the art
    // it lights at spawn -- the prop under its box, or, for a person's encounter,
    // that person's body by name (see glimmer::spawn).
    glimmer::spawn(em, gs.psyche, gs.glimmer_config, gs.region_npcs, gs.gone);
}

// Load the level `level` (empty = the configured start) into the world (fatal if it
// fails -- the region IS the map). See docs/design/MAP-PIPELINE.md.
bool setupRegion(Engine& engine, EntityManager& em, GameState& gs, const std::string& level,
                 const std::string& spawn_id)
{
    const world_config::Config& wc = gs.world_config;
    const ldtk::Region region =
        ldtk::load(wc.ldtk, wc.tileset_png, gs.surface_config, gs.structure_config, level);
    std::fprintf(stderr,
                 "[region] ldtk load %s: '%s' %dx%d, %zu spawns, %zu warps, %zu props, %zu "
                 "encounters, %zu pickups\n",
                 region.ok ? "OK" : "FAILED", region.level_id.c_str(), region.map.width,
                 region.map.height, region.spawns.size(), region.warps.size(), region.props.size(),
                 region.encounters.size(), region.pickups.size());
    if (!region.ok)
    {
        // The LDtk region IS the map -- no fallback. A failed load (missing/corrupt file)
        // is fatal: the caller aborts rather than launch into an empty world.
        std::fprintf(stderr, "[region] FATAL: could not load the region; aborting.\n");
        return false;
    }
    applyRegion(engine, em, gs, region, spawn_id);
    return true;
}

// Swap the world to another level mid-walk (a warp crossed). Transactional: the target
// is loaded BEFORE the current world is torn down, so a bad warp target leaves the
// pilgrim standing where they were instead of in a void. Pilgrim state (growth, satchel,
// record, clock) lives in GameState and passes through untouched -- only the region's
// entities are rebuilt, exactly as a fresh enter builds them (rebuild-from-authored;
// the spawners re-filter against gs.gone).
bool switchRegion(Engine& engine, EntityManager& em, GameState& gs, const std::string& level,
                  const std::string& spawn_id)
{
    const world_config::Config& wc = gs.world_config;
    const ldtk::Region region =
        ldtk::load(wc.ldtk, wc.tileset_png, gs.surface_config, gs.structure_config, level);
    if (!region.ok)
        return false;

    em.registry().clear();
    gs.player = entt::null;
    applyRegion(engine, em, gs, region, spawn_id);
    head_marker::spawn(em, gs.head_marker_config);
    // Everything above spawned mid-tick; anchor it before a frame renders it.
    anchorInterpolation(em);
    std::fprintf(stderr, "[region] -> '%s' (%zu warps)\n", gs.region.c_str(),
                 gs.region_warps.size());
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
    // Player-voiced lines (`say` deeds, spoken thoughts) speak under the pilgrim's
    // chosen name. Set AFTER resetAuthoredState (which reloads psyche) so it
    // survives the reset.
    gs.psyche.player_name = pilgrim->name.empty() ? pilgrim->id : pilgrim->name;
    // Authored text speaks his chosen name: every {player} placeholder binds now,
    // in place, so all downstream surfaces (box, notebook, pause) read resolved
    // words with no per-surface logic.
    psyche::bindPlayerName(gs.psyche, gs.psyche.player_name);

    // A quit mid-warp-fade leaves the fade phase behind; a new walk starts lit.
    gs.warp_fade = {};

    // Sounds are the last walk's too: silence them, reset the per-walk state, then
    // arm it against the restored flags -- a start_on_flag whose flag this pilgrim
    // already holds must NOT replay at boot (the TV's clunk belongs to the moment
    // it was turned off, not to every load after).
    ambience::stopAll(gs.ambience_state, gs.ambience_config);
    gs.ambience_state = {};
    ambience::arm(gs.ambience_state, gs.ambience_config, gs.psyche.flags);

    seedEdgeDetectors(gs);

    // Terrain + player + props, filtered by the walk above. The region IS the map -- a
    // failed load is fatal. A walk resumes in the level it left; a fresh one starts
    // where the map's default (id-less) PlayerSpawn is -- the spawn IS the start, so
    // there is no config twin to drift from the map (empty = the project's first
    // level). A saved level that no longer exists (renamed/removed in authoring)
    // falls back to the start rather than stranding the walk at the title -- same
    // policy as a resume point that is no longer standable (below).
    const std::string start = ldtk::findStartLevel(gs.world_config.ldtk);
    const std::string level =
        pilgrim->place.walked && !pilgrim->place.region.empty() ? pilgrim->place.region : start;
    if (!setupRegion(engine, em, gs, level, /*spawn_id=*/{}))
    {
        if (level == start)
            return false;
        std::fprintf(stderr, "[save] saved level '%s' no longer exists -- starting from '%s'\n",
                     level.c_str(), start.empty() ? "(first level)" : start.c_str());
        if (!setupRegion(engine, em, gs, start, /*spawn_id=*/{}))
            return false;
    }

    if (!pilgrim->place.walked)
    {
        // They have never set out: leave them at the map's spawn (NOT the saved place,
        // which is meaningless before a first step). He starts with NOTHING -- the
        // notebook and the watch are lying in his room to be picked up, and what
        // they buy (writing a thought down, reading the hour) is missing until he
        // does. The clock opens at the authored moment (config start_time): a
        // specific late morning, not midnight -- he overslept before the first frame.
        gs.clock.seconds = gs.clock.start_seconds;
    }
    else if ((pilgrim->place.region.empty() || pilgrim->place.region == gs.region) &&
             player_movement::canStand(em, gs.player, pilgrim->place.x, pilgrim->place.y))
    {
        // Restore the resume point only in the level it was saved in (a region-fallback
        // above means these coordinates belong to a map that no longer exists -- they
        // could be "standable" in the wrong one by coincidence). An empty saved region
        // is a pre-multi-level save; its coordinates are this world's.
        placePlayer(em, gs, pilgrim->place.x, pilgrim->place.y);
    }
    else
    {
        // The spot is no longer somewhere they can BE: a walk saved against an older map
        // (or a hand-edited file) can name a place that is now water, inside a rock, or off
        // the world entirely. Falling back to the spawn keeps a stale walk playable instead
        // of stranding them in the void.
        std::fprintf(stderr,
                     "[save] resume point (%.0f,%.0f) is not standable here -- "
                     "starting from the map's spawn instead\n",
                     static_cast<double>(pilgrim->place.x), static_cast<double>(pilgrim->place.y));
    }

    // The over-head thought bubble: one player-attached sprite that pops (faculty-hued)
    // while a thought reading is on screen. See HeadMarker / docs/design/HUD.md.
    head_marker::spawn(em, gs.head_marker_config);

    // The region's ambient bed. Loops with a slow fade-in so the world eases in rather
    // than snapping on. Low volume -- the score is sparse and unhurried (see
    // docs/design/AESTHETIC.md). Runs silent if no audio device. A gated score waits:
    // it first sounds when ambient_gate_flag lands (the game loop watches for it), so
    // the opening belongs to the room's own noises, not the soundtrack.
    gs.music_started = gs.world_config.ambient_gate_flag.empty() ||
                       gs.psyche.flags.count(gs.world_config.ambient_gate_flag) > 0;
    if (gs.music_started)
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
    psyche::load(gs.psyche, "config/psyche.json", "config/actions.json");
    growth::load(gs.growth, "config/faculties.json");
    footsteps::load(gs.footstep_config, "config/footsteps.json");
    surfaces::load(gs.surface_config, "config/surfaces.json");
    structures::load(gs.structure_config, "config/structures.json");
    glimmer::load(gs.glimmer_config, "config/glimmer.json"); // before setupRegion (spawns glimmers)
    formulas::load(gs.formulas, "config/formulas.json");     // stat-driven formulas (glow, ...)
    world_items::load(gs.world_items_config, "config/world_items.json");        // floor item feel
    interaction_mode::load(gs.int_mode_config, "config/interaction_mode.json"); // stance badge
    watch_hud::load(gs.watch_hud_config, "config/watch_hud.json");              // watch readout
    spirit_hud::load(gs.spirit_hud_config, "config/spirit_hud.json");           // Spirit total
    world_config::load(gs.world_config, "config/world.json"); // region asset paths

    inventory::load(gs.items, "config/items");
    npc::load(gs.npcs,
              "config/npcs"); // authored characters (bodies; speech is observation content)
    scene::load(gs.scenes, "config/scenes");                    // choreography that plays the graph
    ambience::load(gs.ambience_config, "config/ambience.json"); // named world-sound channels
    yields::load(gs.yield_tables, "config/yields");             // what placed things give up
    crafting::load(gs.recipes, "config/recipes");               // recipes (combine -> made thing)
    crafting::loadConfig(gs.crafting_config, "config/crafting.json"); // outcome/XP tuning

    head_marker::load(gs.head_marker_config, "config/head_marker.json");

    // Authored threads. Their ROUTES are authoring apparatus -- checked here against what the
    // content can actually produce, so a renamed flag that strands a route is reported at boot
    // rather than found in play (warnings only; a broken arc never blocks the game). The arcs
    // that carry a written line stay loaded, because those are the agenda.
    arcs::load(gs.threads, "config/arcs.json");
    {
        // The map raises flags too (a clearing's last piece hauled off), so the survey takes
        // both halves -- otherwise a goal the WORLD can reach reads as one nothing can.
        arcs::Producible world = arcs::survey(gs.psyche);
        for (const auto& flag : ldtk::clearingFlags(gs.world_config.ldtk))
            world.flags.insert(flag);
        for (const auto& p : arcs::validate(gs.threads, world))
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
    tutorial::load(gs.tutorial_config, "config/tutorial.json");
    // Every warp id -> its level, so a door that names a door resolves to a level
    // without the map repeating it (ldtk::warpIndex). Once at boot: the map is
    // authored, not generated.
    gs.warp_levels = ldtk::warpIndex(gs.world_config.ldtk);
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
    setRegionSwitch(&switchRegion);
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
