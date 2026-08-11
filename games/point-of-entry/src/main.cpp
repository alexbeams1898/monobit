#include "AreaLoader.h"
#include "Engine.h"
#include "FloorGen.h"
#include "FontManager.h"
#include "SpriteDefLoader.h"
#include "UIRenderer.h"
#include "ecs/AppState.h"
#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "gl/PixelRenderTarget.h"
#include "ops/AreaBuildOps.h"
#include "ops/CaptureUtils.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "ops/SpawnUtils.h"
#include "ops/ZoneUtils.h"
#include "renderers/DebugPanelRenderer.h"
#include "renderers/FloaterRenderer.h"
#include "renderers/HudRenderer.h"
#include "renderers/NotificationRenderer.h"
#include "renderers/PromptRenderer.h"
#include "screens/PauseScreen.h"
#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"
#include "screens/StagingScreen.h"
#include "screens/TitleScreen.h"
#include "systems/AimSystem.h"
#include "systems/AnimationSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CombatSystem.h"
#include "systems/DamageSystem.h"
#include "systems/FlowFieldSystem.h"
#include "systems/GaitSystem.h"
#include "systems/PickupSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RenderSystem.h"
#include "systems/RewardSystem.h"
#include "systems/SpriteAnimSystem.h"
#include "systems/ThermosSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/TravelSystem.h"
#include "systems/WaveSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

#include <glad/glad.h>

// Point of Entry -- the skeleton. Right now it does one thing: build a floor out
// of hand-authored ASCII rooms, put the player in it, and let him walk around.
// Everything else (the gadget, sealing, the swarm) arrives on top of this.

namespace
{
// THE INTERNAL RESOLUTION IS DERIVED, not fixed. Everything renders at window/zoom and the
// whole buffer is then scaled up by that integer -- the pixel-art pipeline.
//
// Derived rather than constant because a FIXED internal size means the same character is a
// different physical size on every display: 1280x720 fills a 1440p monitor at 2x and a laptop
// at rather less, so the art shrinks when you undock. Dividing the window instead keeps one
// drawn pixel equal to `zoom` screen pixels everywhere.
//
// One rounding pass, too. Drawing straight to the window means the camera and every sprite
// round to a pixel independently, and at a zoom those errors do not cancel -- things pop
// against each other whenever frame times jitter, which reads as lag.
int internalW(const Engine& engine)
{
    return engine.windowWidth() / debug_panel::zoom();
}
int internalH(const Engine& engine)
{
    return engine.windowHeight() / debug_panel::zoom();
}

// The shell's state, and the handles the callbacks need. File-scope because the engine's
// callbacks are plain function pointers -- there is no user-data slot to thread them through.
app::State sApp;
Engine* sEngine = nullptr;

// The cellar's unlit dark -- what shows where no tile is drawn.
constexpr float kVoidR = 0.05f;
constexpr float kVoidG = 0.05f;
constexpr float kVoidB = 0.06f;

// Placeholder art: the engine draws a coloured quad for a sprite with no
// texture, which is all this needs until the real pixel art exists.
constexpr float kPlayerSize = 28.0f;
constexpr float kPoeSize = 32.0f;

// THE DEATH BEAT. No drama, no announcement, no question -- the register is a
// man who never cracks, so the world fades to black and he is simply home,
// whole. The game never explains how he got there. The world FREEZES for the
// duration: watching the swarm keep boiling while you fade would read as the
// game continuing without you, which is a different (and wrong) statement.
enum class DeathBeat
{
    None,
    FadeOut,
    FadeIn
};
DeathBeat sDeathBeat = DeathBeat::None;
float sDeathTimer = 0.0f;
constexpr float kDeathFadeOut = 0.7f;
constexpr float kDeathFadeIn = 0.7f;

// Where this job began: dying puts him back here. Held by the shell because the floor that
// spawned him is long out of scope by the time he dies on it.
float sSpawnX = 0.0f;
float sSpawnY = 0.0f;

// Opening a floor from the authored world (the dig site) and from the title
// (the fallback with no map) share one generator.
bool generateFloor(Engine& engine, int depth);

// A teleport is a cut: position, interpolation history, and the camera's
// history all move together, or the first frame after the cut smears.
void cutPlayerTo(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    auto& t = reg.get<Transform>(p);
    t.x = x;
    t.y = y;
    reg.get<PreviousTransform>(p) = PreviousTransform{x, y};
    if (auto* cam = reg.try_get<Camera>(p))
    {
        cam->x = x;
        cam->y = y;
        cam->prev_x = x;
        cam->prev_y = y;
    }
}

// The exterminator himself, created once and carried through every world he
// enters. No-op when he already exists -- his sheet and pocket are him.
void ensurePlayer(EntityManager& em, float x, float y)
{
    if (em.registry().valid(player::entity()))
        return;
    const entt::entity playerEnt = spawn::box(em, x, y, kPlayerSize, 0.85f, 0.84f, 0.78f);
    // Real art, if the pipeline has produced any. No def means the placeholder
    // box stands in, loudly (SpriteDef::load logs).
    if (const sprite_def::Def def = sprite_def::load("assets/sprites/player.json"); def.ok)
    {
        auto& spr = em.registry().get<Sprite>(playerEnt);
        spr.texture_path = def.sheet;
        spr.src_w = def.frame_w;
        spr.src_h = def.frame_h;
        em.registry().remove<SolidColor>(playerEnt);
        sprite_anim::attach(em, playerEnt, def);
    }
    // The foot box. Smaller than the sprite and at its bottom, so his torso
    // may overlap walls above him (top-down depth) while his feet stay out.
    em.registry().emplace<Collider>(playerEnt, Collider{16.0f, 12.0f});
    // No authored health: the sheet is the only source, and the numbers derive from it.
    em.registry().emplace<Stats>(playerEnt, stats::playerStart());
    stats::applyDerivations(em, playerEnt);
    // The tank is part of the man, not a side effect of the first shot -- it
    // must read true on the HUD in places where the trigger never fires.
    em.registry().emplace<Charge>(playerEnt,
                                  Charge{tools::chargeTuning().max, tools::chargeTuning().max});
    em.registry().emplace<Camera>(playerEnt, Camera{x, y});
    player::bind(playerEnt);
}

void loadConfigs()
{
    stats::load("config/stats.json");
    thermos::load("config/stats.json");
    items::load("config/items");
    tools::load("config/tools.json");
}

// The dig site in reach, if any.
entt::entity digSiteInRange(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return entt::null;
    const auto& pt = reg.get<Transform>(p);
    for (const auto [e, t, site] : reg.view<Transform, DigSite>().each())
    {
        const float dx = pt.x - t.x;
        const float dy = pt.y - t.y;
        if (dx * dx + dy * dy < site.radius * site.radius)
            return e;
    }
    return entt::null;
}

// DEATH. It costs the floor's progress and the walk back down -- nothing
// else. He wakes at home, whole, the pocket with him; the pocket is the POINT
// of dying being cheap: the walk back down is already the tension, and taking
// the money too would punish the same mistake twice.
void deathReturn(Engine& engine, EntityManager& em)
{
    auto& reg = em.registry();
    floaters::clear();
    notify::clear();

    const std::string start = area::startLevel("assets/maps/world.ldtk");
    if (start.empty() || !travel::enter(engine, em, start))
    {
        // No authored home: the old behavior, the floor flooding back under him.
        std::vector<entt::entity> gone;
        for (const auto e : reg.view<Vermin>())
            gone.push_back(e);
        for (const auto e : reg.view<HitArea>())
            gone.push_back(e);
        for (const auto e : reg.view<Particle>())
            gone.push_back(e);
        for (const auto e : gone)
            reg.destroy(e);
        swarm::restart();
        cutPlayerTo(em, sSpawnX, sSpawnY);
    }

    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    if (auto* hp = reg.try_get<Health>(p))
        hp->current = hp->max;
    if (auto* sta = reg.try_get<Stamina>(p))
        sta->current = sta->max_stamina;
    if (auto* charge = reg.try_get<Charge>(p))
        charge->current = charge->max_charge;
    thermos::rest(em); // he wakes as if he had taken his break -- flask full
    poe::log().info("death: he wakes at home");
}

// The world only ticks while it is being PLAYED: not behind the title, and not behind the
// pause screen. Pausing is a thing the world does, not a phase the program enters, so it is a
// flag checked here rather than a separate branch of the shell.
void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    // Entering play swallows whatever the trigger was doing on the menu. Derived from the state
    // rather than called at each transition -- there are several ways in (take a job, resume,
    // back out of settings) and one of them would eventually forget.
    static bool sWasPlaying = false;
    const bool nowPlaying = sApp.phase == app::Phase::Playing && !sApp.paused && !sApp.staging;
    if (nowPlaying && !sWasPlaying)
        aim::requireFreshPress();
    sWasPlaying = nowPlaying;

    if (!nowPlaying)
        return;

    // The death beat owns the clock while it runs: the world holds its breath, the floor is
    // flooded back under cover of the black, and play resumes only once the fade-in ends.
    if (sDeathBeat != DeathBeat::None)
    {
        sDeathTimer += static_cast<float>(dt);
        if (sDeathBeat == DeathBeat::FadeOut && sDeathTimer >= kDeathFadeOut)
        {
            // Full black: he goes home under it, no questions asked.
            deathReturn(engine, em);
            sDeathBeat = DeathBeat::FadeIn;
            sDeathTimer = 0.0f;
        }
        else if (sDeathBeat == DeathBeat::FadeIn && sDeathTimer >= kDeathFadeIn)
        {
            sDeathBeat = DeathBeat::None;
            aim::requireFreshPress(); // a trigger held through death is not an order to fire
        }
        return;
    }

    // A door transition owns the clock exactly as the death beat does: the
    // world holds its breath under the black and the swap happens mid-curtain.
    if (travel::active())
    {
        travel::update(engine, em, static_cast<float>(dt));
        return;
    }

    // Aim first: everything that fires this tick reads where he is pointing, and a stale
    // cursor would put the shot where he pointed last frame.
    aim::update(engine, em);
    player::update(engine, em, dt);

    // After movement, before anything reads the map: crossing a door starts
    // the curtain and the rest of this tick stands down.
    travel::update(engine, em, static_cast<float>(dt));
    if (travel::active())
        return;

    // THE STAGING AREA, the reference's interaction shape: in range, either Space (the interact
    // key) or a click ON the spot itself. Interacting IS resting -- heal, refill, and the staging
    // menu opens where you choose the brew. The click is swallowed so putting the kit down
    // never doubles as a trigger pull.
    if (reward::atRest(em))
    {
        prompt::offer("Rest");
        bool clickedSpot = false;
        if (aim::firePressed())
            for (const auto [e, t, spot] : em.registry().view<Transform, RestSpot>().each())
            {
                const float dx = aim::worldX() - t.x;
                const float dy = aim::worldY() - t.y;
                if (dx * dx + dy * dy < spot.radius * spot.radius)
                    clickedSpot = true;
            }
        if (player::consumeInteract() || clickedSpot)
        {
            thermos::rest(em);
            sApp.staging = true;
            staging_screen::reset();
            aim::requireFreshPress();
            return;
        }
    }
    else if (const entt::entity dig = digSiteInRange(em); dig != entt::null)
    {
        // THE WAY DOWN. Descending is a deliberate act, never a walk-on: you
        // do not fall into the wound by accident.
        prompt::offer("Descend");
        if (player::consumeInteract())
        {
            const int depth = em.registry().get<DigSite>(dig).depth;
            generateFloor(engine, depth);
            aim::requireFreshPress();
            return;
        }
    }
    else
        player::consumeInteract(); // a Space pressed in the field means nothing yet -- drop it
    // The field is rebuilt toward the player, then everything reads it -- see Chase.h.
    const auto& pt = em.registry().get<Transform>(player::entity());
    FlowFieldSystem::update(em, pt.x, pt.y);
    chase::update(em, static_cast<float>(dt));
    swarm::update(em, static_cast<float>(dt));

    // THE FIRST POINT OF ENTRY LEAKS. A dig site with a trickle lets one
    // through every so often; they accumulate until he leaves the room.
    for (const auto [e, t, site] : em.registry().view<Transform, DigSite>().each())
    {
        if (site.trickle.empty())
            continue;
        site.timer += static_cast<float>(dt);
        if (site.timer >= site.interval)
        {
            site.timer = 0.0f;
            swarm::spawnOne(em, site.trickle, t.x, t.y);
        }
    }

    // THE TRADE HAS ITS PLACE: the weapon fires only where vermin can reach
    // him. At the bar he is a man carrying equipment, not a man spraying it.
    if (zone::combat(em))
        tools::update(em, static_cast<float>(dt));
    tools::tickStamina(em, static_cast<float>(dt));
    tools::tickParticles(em, static_cast<float>(dt));
    hit_area::update(em, static_cast<float>(dt));
    reward::update(em, static_cast<float>(dt));
    pickup::update(em);
    floaters::update(static_cast<float>(dt));
    // AFTER the movement that chooses which animation plays, so a frame shows the pose that
    // matches where the character now is rather than trailing it by a tick. Paused above, so a
    // character stops mid-stride instead of walking on the spot behind the menu.
    AnimationSystem::update(em, static_cast<float>(dt));
    // After everything that moves, since the gait is driven by how far things actually went.
    walk_bob::update(em, static_cast<float>(dt));

    // Last, after every system that could have hurt him this tick.
    if (const auto* hp = em.registry().try_get<Health>(player::entity());
        hp != nullptr && hp->current <= 0 && sDeathBeat == DeathBeat::None)
    {
        sDeathBeat = DeathBeat::FadeOut;
        sDeathTimer = 0.0f;
    }
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float /*alpha*/)
{
    // Draw the world at the internal resolution, then blit the whole buffer up. Zoom stays
    // at 1: the scale comes from the upscale, not from the camera, which is what keeps every
    // pixel square and every rounding decision in one place.
    // A zoom change (or a resize) re-makes the target at the new size. Cheap, and it happens
    // only when the number actually moves.
    static int sLastZoom = 0;
    static int sLastW = 0;
    if (debug_panel::zoom() != sLastZoom || engine.windowWidth() != sLastW)
    {
        sLastZoom = debug_panel::zoom();
        sLastW = engine.windowWidth();
        engine::gl::pixelTargetInit(internalW(engine), internalH(engine));
        engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
        RenderSystem::resize(internalW(engine), internalH(engine));
    }

    engine::gl::pixelTargetBegin(kVoidR, kVoidG, kVoidB);
    if (sApp.world_built)
    {
        TileMapRenderer::render(camX, camY, internalW(engine), internalH(engine), 1.0f);
        RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
    }
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());

    // Captures the WORLD only. The shell is drawn by the UI callback, which the engine runs
    // inside UIRenderer begin/endFrame -- its text is still unsubmitted when any game callback
    // returns, so a menu cannot be grabbed from here.
    capture::writeIfRequested(engine.windowWidth(), engine.windowHeight());
}

// Open a generated floor at `depth` -- from a dig site in the authored world,
// or from the title as the no-map fallback. The player walks in if he exists;
// he is created standing at the way in if not.
bool generateFloor(Engine& engine, int depth)
{
    auto& em = engine.entityManager();
    auto& reg = em.registry();
    // The world he came from goes; he does not. Doors from the authored level
    // he left stop existing with it.
    travel::leaveAuthored();
    if (reg.valid(player::entity()))
    {
        std::vector<entt::entity> gone;
        for (const auto e : reg.view<Transform>())
            if (e != player::entity())
                gone.push_back(e);
        for (const auto e : gone)
            reg.destroy(e);
    }
    else
        reg.clear();
    // The tile vocabulary is the floor's own: the authored tileset must not
    // bleed into the dug chamber.
    em.tile_config = TileConfig{};

    const floorgen::Floor floor = floorgen::generate(em, "config/floor.json", "config/rooms");
    if (!floor.ok)
    {
        poe::log().error("world: could not build a floor");
        return false;
    }
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;

    // Every marker the rooms carried. 'P' is a point of entry -- for now a red box standing in
    // the room, which is enough to prove placement works.
    // WHAT KIND of hole each marker becomes: rolled from the floor's own seed, so a layout is
    // the same holes every time it is generated. The kinds and their mix live in floor.json;
    // each kind's file says what it looks like and where it sits (floor pit, or an arch at the
    // base of a wall). The generator stays agnostic throughout -- letters in, meaning here.
    struct SeepKind
    {
        std::string path;
        int weight = 1;
        sprite_def::Def def;
        bool on_wall = false;
    };
    std::vector<SeepKind> kinds;
    {
        std::ifstream in("config/floor.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (!j.is_discarded())
            for (const auto& entry : j.value("seep_types", nlohmann::json::array()))
            {
                SeepKind kind;
                kind.path = entry.value("seep", std::string{});
                kind.weight = entry.value("weight", 1);
                std::ifstream sf(kind.path);
                const nlohmann::json sj =
                    sf ? nlohmann::json::parse(sf, nullptr, /*allow_exceptions=*/false)
                       : nlohmann::json{};
                if (!sj.is_discarded())
                {
                    kind.def = sprite_def::load(sj.value("sprite", std::string{}));
                    kind.on_wall = sj.value("placement", std::string{"floor"}) == "wall";
                }
                kinds.push_back(std::move(kind));
            }
    }

    // Letters an authored room may PIN to a kind: 'M' says this exact spot is a mouse hole.
    // Plain 'P' rolls from the floor's mix. Pinned kinds not already in the mix are loaded on
    // first sight, so a template can place something the floor would never roll.
    std::unordered_map<char, std::string> pinned;
    {
        std::ifstream in("config/floor.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (!j.is_discarded() && j.is_object())
        {
            // NAMED, not a temporary: iterating .items() over the value() temporary is
            // use-after-free -- the copy dies before the loop reads it.
            const nlohmann::json ms = j.value("marker_seeps", nlohmann::json::object());
            for (const auto& [letter, path] : ms.items())
                if (!letter.empty() && path.is_string())
                    pinned.emplace(letter.front(), path.get<std::string>());
        }
    }
    const auto kindByPath = [&kinds](const std::string& path) -> const SeepKind*
    {
        for (const auto& k : kinds)
            if (k.path == path)
                return &k;
        return nullptr;
    };
    const auto loadPinnedKind = [&kinds](const std::string& path) -> const SeepKind*
    {
        SeepKind kind;
        kind.path = path;
        std::ifstream sf(path);
        const nlohmann::json sj =
            sf ? nlohmann::json::parse(sf, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        if (!sj.is_discarded())
        {
            kind.def = sprite_def::load(sj.value("sprite", std::string{}));
            kind.on_wall = sj.value("placement", std::string{"floor"}) == "wall";
        }
        kinds.push_back(std::move(kind));
        return &kinds.back();
    };

    int poeCount = 0;
    std::vector<swarm::Seep> seeps;
    std::mt19937 seepRng(floor.seed * 2654435761u + 97u);
    int totalKindWeight = 0;
    for (const auto& kind : kinds)
        totalKindWeight += kind.weight;
    for (const auto& m : floor.markers)
    {
        const auto pin = pinned.find(m.type);
        if (m.type == 'P' || pin != pinned.end())
        {
            const SeepKind* kind = kinds.empty() ? nullptr : &kinds.front();
            if (pin != pinned.end())
            {
                kind = kindByPath(pin->second);
                if (kind == nullptr)
                    kind = loadPinnedKind(pin->second);
            }
            else if (totalKindWeight > 0)
            {
                std::uniform_int_distribution<int> roll(0, totalKindWeight - 1);
                int ticket = roll(seepRng);
                for (const auto& k : kinds)
                {
                    ticket -= k.weight;
                    if (ticket < 0)
                    {
                        kind = &k;
                        break;
                    }
                }
            }

            const entt::entity hole = spawn::box(em, m.x, m.y, kPoeSize, 0.75f, 0.15f, 0.15f);
            // Where creatures actually surface. THE SPAWN MOVES WITH THE ART: a wall-mounted
            // hole that kept emitting at its distant marker would be scenery beside an
            // invisible fountain -- the one thing a spawner may never be is somewhere other
            // than where it spawns.
            float seepX = m.x;
            float seepY = m.y;
            if (kind != nullptr && kind->def.ok)
            {
                auto& spr = em.registry().get<Sprite>(hole);
                spr.texture_path = kind->def.sheet;
                spr.src_w = kind->def.frame_w;
                spr.src_h = kind->def.frame_h;
                spr.layer = 1;
                em.registry().remove<SolidColor>(hole);
                if (kind->on_wall)
                {
                    // An arch at the lower CENTRE of the nearest wall above -- snapped to the
                    // tile so it reads as architecture, bottom flush with the wall's base so it
                    // touches the floor it feeds. Creatures surface at its mouth. No wall in
                    // reach: the art stays a floor pit, better honest than floating.
                    const auto ts = static_cast<float>(em.tile_map.tile_size);
                    for (int step = 1; step <= 4; ++step)
                    {
                        const float wy = m.y - static_cast<float>(step) * ts;
                        if (!world::walkable(em, m.x, wy))
                        {
                            auto& t = em.registry().get<Transform>(hole);
                            t.x = std::floor(m.x / ts) * ts + ts * 0.5f;
                            const float wallBottom = std::floor(wy / ts) * ts + ts;
                            t.y = wallBottom - static_cast<float>(kind->def.frame_h) * 0.5f;
                            em.registry().get<Sprite>(hole).layer = 2;
                            seepX = t.x;
                            seepY = wallBottom + 8.0f; // the floor at the arch's mouth
                            break;
                        }
                    }
                }
            }
            poe::log().info("world: marker '{}' ({:.0f},{:.0f}) -> '{}', art ({:.0f},{:.0f}), "
                            "spawn ({:.0f},{:.0f})",
                            std::string(1, m.type), m.x, m.y,
                            kind != nullptr ? kind->path : "<none>",
                            em.registry().get<Transform>(hole).x,
                            em.registry().get<Transform>(hole).y, seepX, seepY);
            seeps.push_back(
                swarm::Seep{seepX, seepY, kind != nullptr ? kind->path : std::string{}});
            ++poeCount;
        }
    }

    ensurePlayer(em, floor.spawn_x, floor.spawn_y);
    cutPlayerTo(em, floor.spawn_x, floor.spawn_y);
    // The rest spot: at the way in. A pale ring of floor where the kit is set down -- the only
    // place points are sold, so the walk back to it with a full pocket is the loop's tension.
    {
        const entt::entity spot =
            spawn::box(em, floor.spawn_x, floor.spawn_y + 8.0f, 22.0f, 0.30f, 0.42f, 0.40f);
        em.registry().emplace<RestSpot>(spot, RestSpot{40.0f});
        em.registry().get<Sprite>(spot).layer = 1; // ground marking, under everything that walks
    }
    sSpawnX = floor.spawn_x;
    sSpawnY = floor.spawn_y;
    poe::log().info("world: player at ({:.0f},{:.0f}), {} points of entry, depth {}", floor.spawn_x,
                    floor.spawn_y, poeCount, depth);
    // Opening the space is what disturbs it. The dig site carries the depth,
    // and with it the law: proximity to the source dictates difficulty --
    // everything downstream reads this one number.
    swarm::begin("config/swarm.json", seeps, depth);
    sApp.world_built = true;
    return true;
}

// A new job from the title. The authored world is home when the map declares
// a start; the bare generated floor stays as the fallback while it does not.
bool buildWorld(Engine& engine)
{
    auto& em = engine.entityManager();
    em.registry().clear(); // a previous job's world -- and its man; a new job is a new sheet
    loadConfigs();
    if (const std::string start = area::startLevel("assets/maps/world.ldtk"); !start.empty())
    {
        ensurePlayer(em, 0.0f, 0.0f);
        if (travel::enter(engine, em, start))
        {
            const auto& t = em.registry().get<Transform>(player::entity());
            sSpawnX = t.x;
            sSpawnY = t.y;
            sApp.world_built = true;
            return true;
        }
        poe::log().error("world: start level '{}' failed to load -- generating instead", start);
    }
    return generateFloor(engine, 0);
}

// What the shell does with what a screen reported.
void enactTitle(Engine& engine, title_screen::Action a)
{
    switch (a)
    {
    case title_screen::Action::NewJob:
    case title_screen::Action::Continue: // no saves yet; both mean "go in" for now
        if (buildWorld(engine))
            sApp.phase = app::Phase::Playing;
        break;
    case title_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Title;
        sApp.phase = app::Phase::Settings;
        break;
    case title_screen::Action::Quit:
        engine.requestQuit();
        break;
    case title_screen::Action::None:
        break;
    }
}

void enactPause(Engine& engine, pause_screen::Action a)
{
    switch (a)
    {
    case pause_screen::Action::Resume:
        sApp.paused = false;
        break;
    case pause_screen::Action::Settings:
        sApp.settings_return_to = app::Phase::Playing;
        sApp.phase = app::Phase::Settings;
        break;
    case pause_screen::Action::Leave:
        sApp.paused = false;
        sApp.world_built = false;
        sApp.phase = app::Phase::Title;
        travel::reset();
        title_screen::reset();
        break;
    case pause_screen::Action::Quit:
        engine.requestQuit();
        break;
    case pause_screen::Action::None:
        break;
    }
}

// The shell, drawn over everything. Which surface is up follows from the phase; pausing is a
// flag on Playing rather than a phase, because the world is still loaded behind it.
void gameRenderUI(Engine& engine, EntityManager& em)
{
    const bool playing = sApp.phase == app::Phase::Playing && !sApp.paused && !sApp.staging;
    // The dev panel needs a pointer to click, and it opens while playing -- so it counts as a
    // screen with options, exactly like a menu.
    hud::cursorForPhase(playing && !debug_panel::visible());
    if (playing)
    {
        // World-anchored, so it needs the camera the world was drawn with.
        const auto& cam = em.registry().get<Camera>(player::entity());
        const float cx = std::round(cam.x);
        const float cy = std::round(cam.y);
        hud::renderWorldOverlays(engine, em, cx, cy, debug_panel::zoom());
        floaters::render(engine, cx, cy, debug_panel::zoom());
        hud::render(engine, em);

        // The black, over everything -- the HUD fades with the world.
        if (sDeathBeat != DeathBeat::None)
        {
            const float a = (sDeathBeat == DeathBeat::FadeOut) ? sDeathTimer / kDeathFadeOut
                                                               : 1.0f - sDeathTimer / kDeathFadeIn;
            UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(engine.windowWidth()),
                                 static_cast<float>(engine.windowHeight()),
                                 screen_style::black(std::min(1.0f, std::max(0.0f, a))));
        }
        // The door curtain, same cloth as the death black.
        if (travel::active())
            UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(engine.windowWidth()),
                                 static_cast<float>(engine.windowHeight()),
                                 screen_style::black(travel::curtainAlpha()));
    }
    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();

    const shell_input::Frame in = shell_input::read();

    switch (sApp.phase)
    {
    case app::Phase::Title:
    {
        const title_screen::Mouse m{in.mouse_x, in.mouse_y, in.clicked};
        // Keyboard first, then the mouse over what was drawn -- either may commit.
        enactTitle(engine, title_screen::step(in.up, in.down, in.confirm, /*has_save=*/false));
        if (sApp.phase == app::Phase::Title)
            enactTitle(engine, title_screen::render(m, /*has_save=*/false, ww, wh));
        break;
    }
    case app::Phase::Settings:
        // No settings surface yet: anything that leaves goes back where it came from.
        screen_style::dim(ww, wh);
        screen_style::headingCentered("SETTINGS", static_cast<float>(ww) * 0.5f,
                                      static_cast<float>(wh) * 0.35f, screen_style::kText);
        screen_style::textCentered(
            "nothing to set yet -- Esc goes back", static_cast<float>(ww) * 0.5f,
            static_cast<float>(wh) * 0.35f + screen_style::lineHeight() * 2.0f,
            screen_style::kTextDim);
        if (in.back || in.confirm)
            sApp.phase = sApp.settings_return_to;
        break;
    case app::Phase::Playing:
        if (sApp.staging)
        {
            const staging_screen::Mouse m{in.mouse_x, in.mouse_y, in.clicked};
            staging_screen::Action a = staging_screen::step(in.up, in.down, in.confirm, in.back);
            if (a == staging_screen::Action::None)
                a = staging_screen::render(em, m, ww, wh);
            if (a == staging_screen::Action::Close)
                sApp.staging = false;
            break;
        }
        if (!sApp.paused && in.back)
        {
            sApp.paused = true;
            pause_screen::reset();
        }
        else if (sApp.paused)
        {
            const pause_screen::Mouse m{in.mouse_x, in.mouse_y, in.clicked};
            enactPause(engine,
                       pause_screen::step(in.up, in.down, in.left, in.right, in.confirm, in.back));
            if (sApp.paused)
                enactPause(engine, pause_screen::render(em, m, ww, wh));
        }
        break;
    }
}

void gameOnResize(Engine& /*engine*/, int w, int h)
{
    // The blit follows the window, and so does the internal size -- see internalW().
    engine::gl::pixelTargetResize(w, h);
}

} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

#ifdef POE_SOURCE_DIR
    // Dev builds run against the source tree: every relative path (config/,
    // assets/) resolves there, and the editor's saves land where git sees them.
    std::error_code ec;
    std::filesystem::current_path(POE_SOURCE_DIR, ec);
#endif

    Engine engine;
    // BORDERLESS FULLSCREEN, and it is a performance decision as much as a presentation one.
    // In a WINDOW the Windows compositor (DWM) owns the present: it caps the swap regardless
    // of what SDL_GL_SetSwapInterval asks for, delivering ~57 fps against a 60 Hz tick with
    // periodic 47-76 ms stalls. The fixed-timestep accumulator then slips a tick every so
    // often and the world visibly hitches -- while the game itself renders in 0.04 ms.
    // Going borderless bypasses the compositor and gets a real present. (Wayworn does the
    // same; prison-escape does not, and shows the same hitch.)
    engine.setWindowMode(Engine::WindowMode::BorderlessFullscreen);
    if (!engine.init("Point of Entry", 1280, 720))
        return 1;

    poe::log().info("boot: window is {}x{}", engine.windowWidth(), engine.windowHeight());
    engine::gl::pixelTargetInit(internalW(engine), internalH(engine));
    engine::gl::pixelTargetResize(engine.windowWidth(), engine.windowHeight());
    RenderSystem::init(internalW(engine), internalH(engine));
    TileMapRenderer::init();
    // The engine enables depth testing globally at init for the 3D path. This is a 2D
    // game: everything draws at z=0, so with GL_LESS the tilemap writes depth first and
    // every sprite fails the test and vanishes. Depth order here is the painter's
    // algorithm (RenderSystem sorts by Y), so the test must be off.
    //
    // Worth knowing: the other 2D games only avoid this by ACCIDENT -- UIRenderer
    // disables depth when it submits a batch, and they draw a HUD every frame. A game
    // with no HUD yet hits it immediately, and every diagnostic still reads as correct.
    glDisable(GL_DEPTH_TEST);

    // The one face at its two ladder sizes -- ScreenStyle owns fonts and the integer scale
    // (see check_design.py for the standards this keeps).
    screen_style::initFonts(engine.windowHeight());

    // Authored places: builders first, then the door index over the project.
    travel::init();
    area_build::registerAll();
    travel::scan("assets/maps/world.ldtk");

    auto& em = engine.entityManager();

    engine.setGameUpdate(&gameUpdate);
    engine.setRenderWorld(&gameRenderWorld);
    engine.setOnResize(&gameOnResize);
    engine.setRenderUI(&gameRenderUI);
    engine.setRenderImGui(&debug_panel::render);
    engine.run();

    engine::gl::pixelTargetShutdown();
    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    return 0;
}
