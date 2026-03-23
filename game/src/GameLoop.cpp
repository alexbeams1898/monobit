#include "GameLoop.h"

#include "ConfigLoader.h"
#include "Engine.h"
#include "TileMapLoader.h"
#include "UIRenderer.h"
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
#include "systems/NotificationSystem.h"
#include "systems/ParticleSystem.h"
#include "systems/PickupSystem.h"
#include "systems/RestSpotSystem.h"
#include "systems/SpawnerSystem.h"
#include "systems/TintSystem.h"
#include "systems/WaveSystem.h"

// UI screens / renderers.
#include "renderers/HudRenderer.h"
#include "renderers/InteractionPromptRenderer.h"
#include "screens/LevelUpScreen.h"
#include "screens/PauseMenu.h"

#include <SDL.h>
#include <cmath>
#include <string>
#include <tracy/Tracy.hpp>

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

// Title-bar -- game name + FPS.
static void updateTitleBar(Engine& engine, EntityManager& /*em*/)
{
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Hell Escape  |  FPS " + std::to_string(fps) + "/60");
}

// Handle UI screen toggle inputs and suppress gameplay when a screen is open.
static void updateUIState(EntityManager& em)
{
    auto& ui = em.registry().ctx().get<UIState>();

    for (auto [entity, actions] : em.registry().view<PlayerActions>().each())
    {
        if (actions.toggle_pause)
        {
            if (ui.active_screen == UIState::Screen::None)
            {
                ui.active_screen = UIState::Screen::Menu;
                PauseMenu::reset();
            }
            else
            {
                if (ui.active_screen == UIState::Screen::LevelUp &&
                    em.registry().all_of<Experience>(entity))
                {
                    const auto& exp = em.registry().get<Experience>(entity);
                    NotificationSystem::push(
                        "Level Up! (Lv " + std::to_string(exp.level) + ")",
                        {1.0f, 0.85f, 0.3f, 1.0f});
                }
                ui.active_screen = UIState::Screen::None;
            }
        }

        // I key opens menu to Inventory tab (or closes if already on it).
        if (actions.toggle_inventory)
        {
            if (ui.active_screen == UIState::Screen::Menu &&
                ui.menu_tab == UIState::Tab::Inventory)
            {
                ui.active_screen = UIState::Screen::None;
            }
            else if (ui.active_screen == UIState::Screen::None)
            {
                ui.active_screen = UIState::Screen::Menu;
                ui.menu_tab = UIState::Tab::Inventory;
                PauseMenu::reset();
            }
        }

        // C key opens menu to Crafting tab (or closes if already on it).
        if (actions.craft)
        {
            if (ui.active_screen == UIState::Screen::Menu &&
                ui.menu_tab == UIState::Tab::Crafting)
            {
                ui.active_screen = UIState::Screen::None;
            }
            else if (ui.active_screen == UIState::Screen::None)
            {
                ui.active_screen = UIState::Screen::Menu;
                ui.menu_tab = UIState::Tab::Crafting;
                PauseMenu::reset();
            }
        }

        // Auto-open level-up screen when the player gains stat points.
        if (ui.active_screen == UIState::Screen::None)
        {
            if (em.registry().all_of<Experience>(entity))
            {
                const auto& exp = em.registry().get<Experience>(entity);
                if (exp.stat_points > 0)
                {
                    ui.active_screen = UIState::Screen::LevelUp;
                    LevelUpScreen::reset();
                }
            }
        }

        // Suppress all gameplay inputs when a screen is open.
        if (ui.isScreenOpen())
        {
            actions.move_x = 0.0f;
            actions.move_y = 0.0f;
            actions.attack = false;
            actions.dodge = false;
            actions.skill = false;
            actions.sprint = false;
            actions.block_held = false;
            actions.block_just_pressed = false;
            actions.interact = false;
            actions.mouse_click = false;
            actions.start_wave = false;
            actions.craft = false;
        }
        break;
    }
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    ZoneScopedN("gameUpdate");

    auto& waveState = em.registry().ctx().get<WaveState>();
    const auto& ui = em.registry().ctx().get<UIState>();

    // Always process input mapping (needed for UI toggles).
    InputMappingSystem::update(em);
    updateUIState(em);

    // When a UI screen is open, freeze the game world.
    if (ui.isScreenOpen())
    {
        // Still clear event buffers (already consumed by InputMappingSystem).
        handleMapRegen(engine, em);
        updateTitleBar(engine, em);
        return;
    }

    // GameOver: freeze all gameplay. Only WaveSystem runs (handles R-key restart).
    if (waveState.phase == WaveState::Phase::GameOver)
    {
        WaveSystem::update(em, dt);
    }
    else
    {
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

void gameRenderUI(Engine& engine, EntityManager& em)
{
    ZoneScopedN("gameRenderUI");

    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();
    const auto& ui = em.registry().ctx().get<UIState>();

    // Show system cursor only when menu is open (PauseMenu needs mouse interaction).
    SDL_ShowCursor(ui.isScreenOpen() ? SDL_ENABLE : SDL_DISABLE);

    // HUD is always drawn (unless a full-screen menu hides it).
    if (!ui.isScreenOpen())
    {
        HudRenderer::render(em, ww, wh);

        // Clickable menu button (bottom-right).
        if (HudRenderer::renderMenuButton(em, ww, wh))
        {
            em.registry().ctx().get<UIState>().active_screen = UIState::Screen::Menu;
            PauseMenu::reset();
        }

        // Interaction prompt near targeted items.
        float camX = 0.0f, camY = 0.0f;
        for (auto [entity, camera] : em.registry().view<Camera>().each())
        {
            if (camera.active)
            {
                camX = camera.x;
                camY = camera.y;
                break;
            }
        }
        InteractionPromptRenderer::render(em, camX, camY, ww, wh);
    }

    // Notifications always render (even over menus).
    NotificationSystem::render(static_cast<float>(engine.lastFrameTime()), ww, wh);

    // Character menu overlay.
    if (ui.active_screen == UIState::Screen::Menu)
    {
        if (PauseMenu::render(em, ww, wh))
            engine.requestQuit();
    }

    // Level-up overlay.
    if (ui.active_screen == UIState::Screen::LevelUp)
        LevelUpScreen::render(em, ww, wh);

    // Clear event buffers after all UI screens have consumed them.
    em.key_down_events.clear();
    em.mouse_down_events.clear();
}