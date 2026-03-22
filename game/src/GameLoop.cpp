#include "GameLoop.h"

#include "ConfigLoader.h"
#include "CraftingOps.h"
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
#include "systems/CraftingSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DeathSystem.h"
#include "systems/EquipmentSystem.h"
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

    // Reposition player at the bonfire (rest room).
    float spawnX = px;
    float spawnY = py;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'R')
        {
            spawnX = sp.x;
            spawnY = sp.y;
            break;
        }
    }
    for (auto pe : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(pe))
        {
            auto& t = em.registry().get<Transform>(pe);
            t.x = spawnX;
            t.y = spawnY;
        }
        if (em.registry().all_of<Camera>(pe))
        {
            auto& cam = em.registry().get<Camera>(pe);
            cam.x = spawnX;
            cam.y = spawnY;
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

static std::string buildWaveStr(EntityManager& em)
{
    const auto& ws = em.registry().ctx().get<WaveState>();
    const auto& wc = em.registry().ctx().get<WaveConfig>();
    if (!wc.loaded)
        return {};

    std::string s = "  |  WAVE " + std::to_string(ws.current_wave);
    if (wc.gen.max_waves > 0)
        s += "/" + std::to_string(wc.gen.max_waves);
    if (ws.phase == WaveState::Phase::Idle || ws.phase == WaveState::Phase::SafeRoom)
        s += " [R]";
    else if (ws.phase == WaveState::Phase::GameOver)
        s += " GAME OVER [R]";
    else if (ws.phase == WaveState::Phase::Complete)
        s += " DONE";
    return s;
}

static std::string buildCraftHint(EntityManager& em, entt::entity entity)
{
    if (!em.registry().all_of<Inventory>(entity))
        return {};
    const auto& inv = em.registry().get<Inventory>(entity);
    const auto& recipes = em.registry().ctx().get<RecipeRegistry>();
    const auto& itemReg = em.registry().ctx().get<ItemRegistry>();
    const auto* r = CraftingOps::findCraftable(inv, recipes, itemReg);
    return (r != nullptr) ? ("  |  [C] Craft " + r->name) : std::string{};
}

static std::string buildPickupHint(EntityManager& em, entt::entity player)
{
    if (!em.registry().all_of<InteractTarget>(player))
        return {};
    const auto& target = em.registry().get<InteractTarget>(player);
    if (target.entity == entt::null || !em.registry().valid(target.entity))
        return {};
    if (!em.registry().all_of<Pickup>(target.entity))
        return {};
    const auto& pickup = em.registry().get<Pickup>(target.entity);
    if (pickup.item.empty())
        return {};
    const auto& items = em.registry().ctx().get<ItemRegistry>();
    const ItemDef* def = items.find(pickup.item.config_path);
    const std::string name = (def != nullptr) ? def->name : "item";
    const Rarity rarity = (def != nullptr) ? def->rarity : Rarity::Common;
    const bool isMoney = (def != nullptr && def->category == ItemCategory::Money);
    std::string hint = "  |  [F/Click] " + name + " (Rarity: " + rarityName(rarity);
    if (!isMoney)
        hint += ", Quality: " + std::string(qualityName(pickup.item.quality));
    hint += ")";
    return hint;
}

// Build weapon attack/grade string and compute effective ATK for the title bar.
static void buildWeaponInfo(EntityManager& em, entt::entity entity, const Stats& stats,
                            const FormulaConfig& f, int& atk, std::string& atkModStr,
                            std::string& weaponGrade)
{
    if (!em.registry().all_of<Weapon>(entity))
        return;

    const auto& w = em.registry().get<Weapon>(entity);
    const int base = static_cast<int>(w.base_damage);
    const float raw = computeDamage(w, stats, f);
    const int strDef = std::max(0, w.str_requirement - stats.str);
    const int dexDef = std::max(0, w.dex_requirement - stats.dex);
    const float penalty = std::exp(-static_cast<float>(strDef) * f.stat_requirement.penalty_rate) *
                          std::exp(-static_cast<float>(dexDef) * f.stat_requirement.penalty_rate);
    atk = static_cast<int>(raw * penalty);
    const int net = atk - base;
    if (net >= 0)
        atkModStr = " (+" + std::to_string(net) + ")";
    else
        atkModStr = " (" + std::to_string(net) + ")";

    const bool hasWeapon = em.registry().all_of<Equipment>(entity) &&
                           !em.registry().get<Equipment>(entity).main_hand.empty();
    if (hasWeapon)
    {
        const auto& eq = em.registry().get<Equipment>(entity);
        const std::string qName = qualityName(eq.main_hand.quality);
        const int displayLbs = static_cast<int>(std::round(w.weight * 3.0f));
        weaponGrade = "  ---  WPN: " + qName + " " + w.name + "  " + std::to_string(displayLbs) +
                      " lbs  str " + gradeChar(w.str_scaling, f) + " / dex " +
                      gradeChar(w.dex_scaling, f);
        if (w.str_requirement > 0 || w.dex_requirement > 0)
        {
            weaponGrade += "  [Req: STR " + std::to_string(w.str_requirement) + " DEX " +
                           std::to_string(w.dex_requirement) + "]";
        }
    }
    else
    {
        weaponGrade = "  ---  WPN: None Equipped";
    }
}

// Build stamina-aware ATK and STA labels with exhaustion/too-heavy markers.
static void buildStaminaLabels(EntityManager& em, entt::entity entity, const FormulaConfig& f,
                               int atk, const std::string& atkModStr, std::string& atkLabel,
                               std::string& staLabel)
{
    int staPct = 100;
    float staCurrent = 999.0f;
    if (em.registry().all_of<Stamina>(entity))
    {
        const auto& sta = em.registry().get<Stamina>(entity);
        staCurrent = sta.current;
        if (sta.max_stamina > 0.0f)
            staPct = static_cast<int>(std::round(sta.current / sta.max_stamina * 100.0f));
    }

    float swingCost = 0.0f;
    float dodgeCost = 0.0f;
    if (em.registry().all_of<Weapon>(entity))
    {
        const auto& w = em.registry().get<Weapon>(entity);
        swingCost = w.weight * f.stamina.swing_effort;
        dodgeCost = w.weight * f.stamina.dodge_effort;
    }
    float staMax = 0.0f;
    if (em.registry().all_of<Stamina>(entity))
        staMax = em.registry().get<Stamina>(entity).max_stamina;

    atkLabel = "ATK " + std::to_string(atk) + atkModStr;
    if (staCurrent < swingCost)
        atkLabel += (staMax < swingCost) ? " [TOO HEAVY]" : " [EXHAUSTED]";
    staLabel = "STA " + std::to_string(staPct) + "%";
    if (staCurrent < dodgeCost)
        staLabel += (staMax < dodgeCost) ? " [TOO HEAVY]" : " [EXHAUSTED]";
}

// Title-bar HUD -- cheapest possible stat display, no font rendering needed.
static void updateTitleBar(Engine& engine, EntityManager& em)
{
    for (auto [entity, actions, health, stats, exp] :
         em.registry().view<PlayerActions, Health, Stats, Experience>().each())
    {
        const auto& f = em.registry().ctx().get<FormulaConfig>();

        int atk = 5;
        std::string atkModStr;
        std::string weaponGrade;
        buildWeaponInfo(em, entity, stats, f, atk, atkModStr, weaponGrade);

        const int baseDef =
            em.registry().all_of<Body>(entity) ? em.registry().get<Body>(entity).base_defense : 0;
        const int def = static_cast<int>(std::min(
            f.defense.cap, std::floor(static_cast<float>(baseDef) +
                                      static_cast<float>(stats.str) * f.defense.str_scale +
                                      static_cast<float>(stats.end) * f.defense.end_scale +
                                      static_cast<float>(exp.level) * f.defense.level_scale)));

        const int poise = em.registry().all_of<Poise>(entity)
                              ? static_cast<int>(em.registry().get<Poise>(entity).max)
                              : 0;

        std::string atkLabel;
        std::string staLabel;
        buildStaminaLabels(em, entity, f, atk, atkModStr, atkLabel, staLabel);

        const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));

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
                        "  LCK " + std::to_string(stats.lck) + "  |  " + atkLabel + "  DEF " +
                        std::to_string(def) + "  POISE " + std::to_string(poise) + "  " + staLabel;
        }

        std::string statusStr;
        if (em.registry().all_of<Staggered>(entity))
            statusStr += " [STAGGERED]";

        std::string moneyStr;
        if (em.registry().all_of<Wallet>(entity))
            moneyStr = "  $" + std::to_string(em.registry().get<Wallet>(entity).money);

        std::string title = "Hell Escape"
                            "  |  FPS " +
                            std::to_string(fps) + "/60  |  HP " + std::to_string(health.current) +
                            "/" + std::to_string(health.max) + "  |  LVL " +
                            std::to_string(exp.level) + "  XP " + std::to_string(exp.current_xp) +
                            "/" + std::to_string(exp.xp_to_next) + moneyStr + statBlock +
                            weaponGrade + statusStr + buildWaveStr(em) +
                            buildPickupHint(em, entity) + buildCraftHint(em, entity);
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
        // 1. Equipment sync (equipped items -> Weapon/Shield components).
        // 2. Spawning/waves first (new entities enter the world).
        // 3. Combat decisions (hitbox spawning, dodge).
        // 4. Animation state resolution (after combat, before render).
        // 5. Tint (after combat + anim state, before render).
        // 6. AI (aggro, pathfinding, chase, steering).
        // 7. Movement + collision.
        // 8. Damage resolution.
        // 9. Death + cleanup.
        // 10. Progression (pickups, leveling, rest).
        // 11. Camera (snaps to final player position).
        InputMappingSystem::update(em);
        PickupSystem::update(em);
        EquipmentSystem::update(em);
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
        CraftingSystem::update(em);
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
    ZoneScopedN("gamePerFrame");
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

        // Mouse world position for interaction targeting (screen → world via camera).
        if (em.registry().all_of<Camera>(entity))
        {
            const auto& cam = em.registry().get<Camera>(entity);
            actions.mouse_world_x = static_cast<float>(mouseX) -
                                    static_cast<float>(engine.windowWidth()) * 0.5f + cam.x;
            actions.mouse_world_y = static_cast<float>(mouseY) -
                                    static_cast<float>(engine.windowHeight()) * 0.5f + cam.y;
        }
    }
}