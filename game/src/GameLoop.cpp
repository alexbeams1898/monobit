#include "GameLoop.h"

#include "Engine.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

// Engine systems (called by game code in the correct order).
#include "systems/CameraSystem.h"
#include "systems/CollisionSystem.h"
#include "systems/FlowFieldSystem.h"

// Game systems.
#include "systems/AggroSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CombatSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DeathSystem.h"
#include "systems/LevelingSystem.h"
#include "systems/MovementSystem.h"
#include "systems/PickupSystem.h"
#include "systems/RestSpotSystem.h"
#include "systems/SpawnerSystem.h"
#include "systems/ParticleSystem.h"
#include "systems/SteeringSystem.h"
#include "systems/WaveSystem.h"

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

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    ZoneScopedN("gameUpdate");

    // System call order matters:
    // 1. Spawning/waves first (new entities enter the world).
    // 2. Combat decisions (hitbox spawning, dodge).
    // 3. AI (aggro, pathfinding, chase, steering).
    // 4. Movement + collision.
    // 5. Damage resolution.
    // 6. Death + cleanup.
    // 7. Progression (pickups, leveling, rest).
    // 8. Camera (snaps to final player position).
    WaveSystem::update(em, dt);
    CombatSystem::update(em, dt);
    AggroSystem::update(em);
    FlowFieldSystem::update(em);
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

    // Title-bar HUD — cheapest possible stat display, no font rendering needed.
    for (auto [entity, input, health, stats, exp] :
         em.registry().view<Input, Health, Stats, Experience>().each())
    {
        const auto& f = em.formulas;

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

        // Wave info.
        const auto& ws = em.wave_state;
        const auto& wc = em.wave_config;
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
        std::string title =
            "Hell Escape"
            "  |  FPS " +
            std::to_string(fps) + "/60  |  HP " + std::to_string(health.current) + "/" +
            std::to_string(health.max) + "  |  LVL " + std::to_string(exp.level) + "  XP " +
            std::to_string(exp.current_xp) + "/" + std::to_string(exp.xp_to_next) + "  |  STR " +
            std::to_string(stats.str) + "  DEX " + std::to_string(stats.dex) + "  END " +
            std::to_string(stats.end) + "  LCK " + std::to_string(stats.lck) + "  pts " +
            std::to_string(exp.stat_points) + "  |  ATK " + std::to_string(atk) + "  DEF " +
            std::to_string(def) + "  POISE " + std::to_string(poise) + weaponGrade + waveStr;
        engine.setWindowTitle(title);
        break;
    }
}
