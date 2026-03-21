#include "GameLoop.h"

#include "ConfigLoader.h"
#include "Engine.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

// Engine systems.
#include "systems/CameraSystem.h"
#include "systems/CollisionSystem.h"
#include "systems/FlowFieldSystem.h"
#include "systems/SteeringSystem.h"
#include "systems/TileMapRenderer.h"

// Game systems.
#include "systems/AggroSystem.h"
#include "systems/AnimStateSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CombatSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DeathSystem.h"
#include "systems/InputMappingSystem.h"
#include "systems/LevelingSystem.h"
#include "systems/MovementSystem.h"
#include "systems/ParticleSystem.h"
#include "systems/PickupSystem.h"
#include "systems/RestSpotSystem.h"
#include "systems/SpawnerSystem.h"
#include "systems/TintSystem.h"
#include "systems/WaveSystem.h"

#include <SDL.h>
#include <cmath>
#include <string>
#include <tracy/Tracy.hpp>

// 0.0 = no scaling; otherwise first threshold the value meets (S down to E).
static const char* gradeChar(float v, const FormulaConfig& f)
{
    if (v <= 0.0f)
        return "-";
    if (v >= f.grade_thresholds.s)
        return "S";
    if (v >= f.grade_thresholds.a)
        return "A";
    if (v >= f.grade_thresholds.b)
        return "B";
    if (v >= f.grade_thresholds.c)
        return "C";
    if (v >= f.grade_thresholds.d)
        return "D";
    return "E";
}

// When WaveSystem signals a new wave, regenerate the tile map and reposition entities.
static void handleMapRegen(Engine& engine, EntityManager& em)
{
    auto& waveState = em.registry().ctx().get<WaveState>();
    if (!waveState.needs_map_regen)
        return;

    waveState.needs_map_regen = false;

    // Destroy old rest spots and pickups (belong to previous map layout).
    {
        std::vector<entt::entity> old;
        for (auto e : em.registry().view<RestSpot>())
            old.push_back(e);
        for (auto e : em.registry().view<Pickup>())
            old.push_back(e);
        for (auto e : old)
            if (em.registry().valid(e))
                em.registry().destroy(e);
    }

    // Regenerate tile map and re-upload to GPU.
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms");
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Force flow field rebuild (wall layout changed).
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;

    // Reposition player at new spawn point.
    for (auto pe : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(pe))
        {
            auto& t = em.registry().get<Transform>(pe);
            t.x = px;
            t.y = py;
        }
        if (em.registry().all_of<Camera>(pe))
        {
            auto& cam = em.registry().get<Camera>(pe);
            cam.x = px;
            cam.y = py;
        }
        break;
    }

    // Spawn rest spots from new map markers.
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
}

// Title-bar HUD -- cheapest possible stat display, no font rendering needed.
static void updateTitleBar(Engine& engine, EntityManager& em)
{
    for (auto [entity, actions, health, stats, exp] :
         em.registry().view<PlayerActions, Health, Stats, Experience>().each())
    {
        const auto& f = em.registry().ctx().get<FormulaConfig>();

        // Computed attack -- base_damage + stat scaling from equipped weapon.
        int atk = 5; // fist baseline
        std::string weaponGrade;
        if (em.registry().all_of<Weapon>(entity))
        {
            const auto& w = em.registry().get<Weapon>(entity);
            atk = static_cast<int>(computeDamage(w, stats, f));

            const std::string& wname = w.name.empty() ? std::string("?") : w.name;
            weaponGrade = "  ---  " + wname + "  str " + gradeChar(w.str_scaling, f) + " / dex " +
                          gradeChar(w.dex_scaling, f);
        }

        // Computed defense -- mirrors DamageSystem::computeDef formula.
        const int def = static_cast<int>(std::min(
            f.defense.cap, std::floor(static_cast<float>(stats.str) * f.defense.str_scale +
                                      static_cast<float>(stats.end) * f.defense.end_scale +
                                      static_cast<float>(exp.level) * f.defense.level_scale)));

        // Poise threshold (0 = staggers on any hit).
        const int poise = em.registry().all_of<Poise>(entity)
                              ? static_cast<int>(em.registry().get<Poise>(entity).max)
                              : 0;

        // Stamina readout -- pool managed by CombatSystem + MovementSystem.
        int staPct = 100;
        if (em.registry().all_of<Stamina>(entity))
        {
            const auto& sta = em.registry().get<Stamina>(entity);
            if (sta.max_stamina > 0.0f)
                staPct = static_cast<int>(std::round(sta.current / sta.max_stamina * 100.0f));
        }

        // Wave info.
        const auto& ws = em.registry().ctx().get<WaveState>();
        const auto& wc = em.registry().ctx().get<WaveConfig>();
        std::string waveStr;
        if (wc.loaded)
        {
            waveStr = "  |  WAVE " + std::to_string(ws.current_wave);
            if (wc.gen.max_waves > 0)
                waveStr += "/" + std::to_string(wc.gen.max_waves);
            if (ws.phase == WaveState::Phase::Idle || ws.phase == WaveState::Phase::SafeRoom)
                waveStr += " [R]";
            else if (ws.phase == WaveState::Phase::GameOver)
                waveStr += " GAME OVER [R]";
            else if (ws.phase == WaveState::Phase::Complete)
                waveStr += " DONE";
        }

        const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));

        // When stat points are available, show the allocation prompt instead of
        // derived combat stats so the player knows to press 1-4.
        std::string statBlock;
        if (exp.stat_points > 0)
        {
            statBlock = "  |  >>> [1] STR  [2] DEX  [3] END  [4] LCK <<<  (" +
                        std::to_string(exp.stat_points) + " pts)";
        }
        else
        {
            statBlock = "  |  STR " + std::to_string(stats.str) + "  DEX " +
                        std::to_string(stats.dex) + "  END " + std::to_string(stats.end) +
                        "  LCK " + std::to_string(stats.lck) + "  |  ATK " + std::to_string(atk) +
                        "  DEF " + std::to_string(def) + "  POISE " + std::to_string(poise) +
                        "  STA " + std::to_string(staPct) + "%";
        }

        std::string title = "Hell Escape"
                            "  |  FPS " +
                            std::to_string(fps) + "/60  |  HP " + std::to_string(health.current) +
                            "/" + std::to_string(health.max) + "  |  LVL " +
                            std::to_string(exp.level) + "  XP " + std::to_string(exp.current_xp) +
                            "/" + std::to_string(exp.xp_to_next) + statBlock + weaponGrade +
                            waveStr;
        engine.setWindowTitle(title);
        break;
    }
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    ZoneScopedN("gameUpdate");

    auto& waveState = em.registry().ctx().get<WaveState>();

    // GameOver: freeze all gameplay. Only WaveSystem runs (handles R-key restart).
    if (waveState.phase == WaveState::Phase::GameOver)
    {
        InputMappingSystem::update(em);
        WaveSystem::update(em, dt);
    }
    else
    {
        // System call order matters:
        // 0. Input mapping (game actions from raw SDL input).
        // 1. Spawning/waves first (new entities enter the world).
        // 2. Combat decisions (hitbox spawning, dodge).
        // 3. Animation state resolution (after combat, before render).
        // 4. Tint (after combat + anim state, before render).
        // 5. AI (aggro, pathfinding, chase, steering).
        // 6. Movement + collision.
        // 7. Damage resolution.
        // 8. Death + cleanup.
        // 9. Progression (pickups, leveling, rest).
        // 10. Camera (snaps to final player position).
        InputMappingSystem::update(em);
        WaveSystem::update(em, dt);
        CombatSystem::update(em, dt);
        AnimStateSystem::update(em);
        TintSystem::update(em, dt);
        AggroSystem::update(em);
        {
            float px = 0.0f, py = 0.0f;
            for (auto pe : em.registry().view<PlayerActions>())
            {
                if (em.registry().all_of<Transform>(pe))
                {
                    const auto& pt = em.registry().get<Transform>(pe);
                    px = pt.x;
                    py = pt.y;
                }
                break;
            }
            FlowFieldSystem::update(em, px, py);
        }
        ChaseSystem::update(em, dt);
        SteeringSystem::update(em);
        MovementSystem::update(em, dt);
        CollisionSystem::update(em);
        DamageSystem::update(em);
        DeathSystem::update(em, dt);
        PickupSystem::update(em);
        LevelingSystem::update(em);
        RestSpotSystem::update(em, dt);
        ParticleSystem::update(em, dt);
        CameraSystem::update(em);
    }

    handleMapRegen(engine, em);
    updateTitleBar(engine, em);
}

void gamePerFrame(Engine& engine, EntityManager& em, double /*dt*/)
{
    // Mouse-aim facing: derived per-frame from raw mouse position so it always
    // reflects the current mouse position. Running this at the fixed-step tick
    // rate causes stale-facing wobble on 0-update frames.
    //
    // render_dx/dy blends toward dx/dy each frame for smooth visual rotation.
    // Gameplay reads dx/dy directly for instant combat response.
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    static constexpr float RENDER_FACING_BLEND = 0.25f;
    for (auto [entity, actions, facing] :
         em.registry().view<PlayerActions, FacingDirection>().each())
    {
        const float sdx =
            static_cast<float>(mouseX) - static_cast<float>(engine.windowWidth()) * 0.5f;
        const float sdy =
            static_cast<float>(mouseY) - static_cast<float>(engine.windowHeight()) * 0.5f;
        const float slen = std::sqrt(sdx * sdx + sdy * sdy);
        if (slen > 8.0f)
        {
            facing.dx = sdx / slen;
            facing.dy = sdy / slen;
        }

        facing.render_dx += (facing.dx - facing.render_dx) * RENDER_FACING_BLEND;
        facing.render_dy += (facing.dy - facing.render_dy) * RENDER_FACING_BLEND;
        const float rl =
            std::sqrt(facing.render_dx * facing.render_dx + facing.render_dy * facing.render_dy);
        if (rl > 0.0f)
        {
            facing.render_dx /= rl;
            facing.render_dy /= rl;
        }
    }
}