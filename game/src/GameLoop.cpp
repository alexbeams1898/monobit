#include "GameLoop.h"

#include "ConfigLoader.h"
#include "Engine.h"
#include "SaveManager.h"
#include "TileMapLoader.h"
#include "UIRenderer.h"
#include "WorldInit.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

// Engine systems.
#include "systems/CameraPanSystem.h"
#include "systems/CameraSystem.h"
#include "systems/CollisionSystem.h"
#include "systems/FlowFieldSystem.h"
#include "systems/SteeringSystem.h"
#include "systems/TileMapRenderer.h"

// Game systems.
#include "systems/AggroSystem.h"
#include "systems/AmbientSoundSystem.h"
#include "systems/AnimStateSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CombatSystem.h"
#include "systems/CraftingSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DeathSystem.h"
#include "systems/EquipmentSystem.h"
#include "systems/InputMappingSystem.h"
#include "systems/LadderSystem.h"
#include "systems/LevelingSystem.h"
#include "systems/MovementSystem.h"
#include "systems/NotificationSystem.h"
#include "systems/ParticleSystem.h"
#include "systems/PickupSystem.h"
#include "systems/ProjectileSystem.h"
#include "systems/RestSpotSystem.h"
#include "systems/SpawnerSystem.h"
#include "systems/TintSystem.h"
#include "systems/WaveSystem.h"
#include "systems/WeaponXPSystem.h"

// UI screens / renderers.
#include "renderers/AIDebugOverlay.h"
#include "renderers/AIRecorder.h"
#include "renderers/DebugOverlay.h"
#include "renderers/HudRenderer.h"
#include "renderers/InteractionPromptRenderer.h"
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
#include "screens/SettingsScreen.h"
#include "screens/VictoryScreen.h"
#include "systems/AudioSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <glad/glad.h>
#include <random>
#include <string>
#include <tracy/Tracy.hpp>

static FontHandle sTitleFont = INVALID_FONT;

void gameLoopInit(FontHandle titleFont)
{
    sTitleFont = titleFont;
}

// Show a "Wave N" loading screen and push it to the display so it stays
// visible during the synchronous map regen that follows.
static void showLoadingOverlay(Engine& engine, int wave)
{
    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();
    const float fw = static_cast<float>(ww);
    const float fh = static_cast<float>(wh);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    UIRenderer::beginFrame();
    UIRenderer::drawRect(0.0f, 0.0f, fw, fh, {0.0f, 0.0f, 0.0f, 1.0f});

    const std::string text = "Wave " + std::to_string(wave);
    const auto ts = UIRenderer::measureText(sTitleFont, text);
    UIRenderer::drawText(sTitleFont, text, (fw - ts.width) * 0.5f, (fh - ts.height) * 0.5f,
                         {1.0f, 0.85f, 0.3f, 1.0f});

    UIRenderer::endFrame();
    engine.swapBuffers();
}

// When WaveSystem signals a new wave, regenerate the tile map and reposition entities.
static void handleMapRegen(Engine& engine, EntityManager& em)
{
    ZoneScopedN("handleMapRegen");
    auto& waveState = em.registry().ctx().get<WaveState>();
    if (!waveState.needs_map_regen)
        return;

    // Render a loading screen before the heavy work so the player sees
    // "Wave N" instead of a frozen frame during the ~600ms regen.
    showLoadingOverlay(engine, waveState.current_wave);

    waveState.needs_map_regen = false;

    // Destroy old rest spots, pickups, ladders, and in-flight projectiles.
    {
        std::vector<entt::entity> old;
        for (auto e : em.registry().view<RestSpot>())
            old.push_back(e);
        for (auto e : em.registry().view<Pickup>())
            old.push_back(e);
        for (auto e : em.registry().view<Ladder>())
            old.push_back(e);
        for (auto e : em.registry().view<Projectile>())
            old.push_back(e);
        for (auto e : old)
            if (em.registry().valid(e))
                em.registry().destroy(e);
    }

    // Regenerate tile map and re-upload to GPU. Grid grows with wave number.
    const int level = std::max(0, waveState.current_wave - 1);
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms", 0, level);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Force flow field rebuild (wall layout changed).
    em.flow_field.last_player_col = -1;
    em.flow_field.last_player_row = -1;

    // Reposition player at the 'P' spawn point (fallback to 'R').
    float spawnX = px;
    float spawnY = py;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'P')
        {
            spawnX = sp.x;
            spawnY = sp.y;
            break;
        }
        if (sp.type == 'R' && spawnX == px && spawnY == py)
        {
            spawnX = sp.x;
            spawnY = sp.y;
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
        // Sync interpolation snapshot so the first frame doesn't blend
        // from the old map position to the new spawn point.
        auto& prev = em.registry().get_or_emplace<PreviousTransform>(pe);
        prev.x = spawnX;
        prev.y = spawnY;
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

    engine.requestTimingReset();
}

// Title-bar -- game name + FPS.
static void updateTitleBar(Engine& engine, EntityManager& /*em*/)
{
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Hell Escape  |  FPS " + std::to_string(fps) + "/60");
}

// Toggle a menu screen to a specific tab: close if already on that tab, open if no screen is up.
static void toggleMenuScreen(UIState& ui, UIState::Tab tab)
{
    if (ui.active_screen == UIState::Screen::Menu && ui.menu_tab == tab)
    {
        ui.active_screen = UIState::Screen::None;
    }
    else if (ui.active_screen == UIState::Screen::None)
    {
        ui.active_screen = UIState::Screen::Menu;
        ui.menu_tab = tab;
        PauseMenu::reset();
    }
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
            else if (ui.active_screen != UIState::Screen::Crafting)
            {
                // CraftingScreen handles its own Escape.
                if (ui.active_screen == UIState::Screen::LevelUp &&
                    em.registry().all_of<Experience>(entity))
                {
                    const auto& exp = em.registry().get<Experience>(entity);
                    NotificationSystem::push("Level Up! (Lv " + std::to_string(exp.level) + ")",
                                             {1.0f, 0.85f, 0.3f, 1.0f});
                }
                ui.active_screen = UIState::Screen::None;
            }
        }

        if (actions.toggle_inventory)
            toggleMenuScreen(ui, UIState::Tab::Inventory);

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

        // Suppress all gameplay inputs when a screen is open, and for one
        // frame after a screen closes (prevents click-through attacks).
        if (ui.isScreenOpen())
        {
            ui.input_suppressed = true;
        }
        if (ui.input_suppressed)
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
            actions.craft = false;
            // Consume mouse buttons so held-click doesn't trigger combat
            // after the screen closes. Persists until button is released.
            em.lmb_consumed = true;
            em.rmb_consumed = true;
            if (!ui.isScreenOpen())
                ui.input_suppressed = false;
        }
        break;
    }
}

// Handle global keybinds (F3-F5, M) that work in all game states.
static void handleGlobalKeys(EntityManager& em)
{
    const auto& kd = em.key_down_events;

    // F3 key: toggle debug overlay.
    if (std::find(kd.begin(), kd.end(), SDL_SCANCODE_F3) != kd.end())
        DebugOverlay::toggle();

    // F4 key: cycle AI debug visualization (Off -> Enemies -> +FlowField -> Off).
    if (std::find(kd.begin(), kd.end(), SDL_SCANCODE_F4) != kd.end())
        AIDebugOverlay::toggle();

    // F5 key: dump AI state recorder to CSV.
    if (std::find(kd.begin(), kd.end(), SDL_SCANCODE_F5) != kd.end())
        AIRecorder::dump();

    // M key: mute/unmute music (persists across track changes).
    if (std::find(kd.begin(), kd.end(), SDL_SCANCODE_M) != kd.end())
        AudioSystem::toggleMusicMute();
}

// Play a music track by config key (looping by default). No-op if missing.
static void playTrack(EntityManager& em, const std::string& key, bool loop = true)
{
    if (auto* t = em.registry().ctx().get<MusicConfig>().get(key))
        AudioSystem::playMusic(t->path, t->volume, loop, t->fade_in_ms);
}

// Play main menu music with a random chance of the rare (reversed) variant.
void playMainMenuMusic(EntityManager& em)
{
    const auto& mc = em.registry().ctx().get<MusicConfig>();
    std::string key = "main_menu";

    if (mc.main_menu_rare_chance > 0.0f && mc.get("main_menu_rare") != nullptr)
    {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        if (dist(rng) < mc.main_menu_rare_chance)
            key = "main_menu_rare";
    }

    playTrack(em, key);
}

// Transition helper: finalize run and go to summary.
static void transitionToSummary(EntityManager& em, bool escaped)
{
    auto& gs = em.registry().ctx().get<GameState>();
    auto& stats = em.registry().ctx().get<RunStats>();
    const auto& cfg = em.registry().ctx().get<ScoringConfig>();
    auto& saveData = em.registry().ctx().get<SaveData>();

    const int score = SaveManager::computeScore(stats, cfg, escaped);
    stats.score = score;

    // Build Run record.
    Run run;
    run.stats = stats;
    run.character_name = gs.active_character;
    run.escaped = escaped;
    // Timestamp.
    const time_t now = time(nullptr);
    char timeBuf[32] = {};
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&now));
    run.timestamp = timeBuf;

    // Check if high score before recording (recording sorts and trims).
    auto top = SaveManager::topRuns(saveData, 10);
    const bool isHighScore =
        (static_cast<int>(top.size()) < 10) || (score > top.back().stats.score);

    // Persist the player's current money to their character profile.
    for (auto pe : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Wallet>(pe))
        {
            const int walletMoney = em.registry().get<Wallet>(pe).money;
            for (auto& prof : saveData.characters)
            {
                if (prof.name == gs.active_character)
                {
                    prof.money = walletMoney;
                    break;
                }
            }
        }
        break;
    }

    SaveManager::recordRun(saveData, run);
    SaveManager::save(saveData);

    RunSummaryScreen::reset(escaped, score, isHighScore);
    gs.phase = GameState::Phase::RunSummary;

    if (escaped)
    {
        AudioSystem::stopMusic();
        playTrack(em, "victory", false);
    }
}

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    ZoneScopedN("gameUpdate");

    auto& gs = em.registry().ctx().get<GameState>();

    // Clear consumption flags only when the button is released, so a pickup
    // click doesn't trigger an attack on the next frame (held state persists).
    const uint32_t mouseState = SDL_GetMouseState(nullptr, nullptr);
    if ((mouseState & SDL_BUTTON_LMASK) == 0)
        em.lmb_consumed = false;
    if ((mouseState & SDL_BUTTON_RMASK) == 0)
        em.rmb_consumed = false;

    handleGlobalKeys(em);

    // Deferred world creation: the loading screen was rendered last frame,
    // so the heavy map generation happens while the overlay is visible.
    if (gs.pending_world_create)
    {
        gs.pending_world_create = false;
        WorldInit::createWorld(engine, em);
        gs.phase = GameState::Phase::Playing;
        engine.requestTimingReset();
        return;
    }

    // Non-playing states: no game systems run.
    if (gs.phase != GameState::Phase::Playing)
    {
        updateTitleBar(engine, em);
        return;
    }

    // --- Playing state ---
    auto& waveState = em.registry().ctx().get<WaveState>();
    const auto& ui = em.registry().ctx().get<UIState>();

    // Always process input mapping (needed for UI toggles).
    InputMappingSystem::update(em);

    updateUIState(em);

    // Suppress player input during camera pan cutscene.
    for (auto pe : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<CameraPan>(pe))
        {
            auto& actions = em.registry().get<PlayerActions>(pe);
            actions.move_x = 0.0f;
            actions.move_y = 0.0f;
            actions.attack = false;
            actions.dodge = false;
            actions.skill = false;
            actions.sprint = false;
            actions.interact = false;
        }
        break;
    }

    // When a UI screen is open, freeze the game world.
    if (ui.isScreenOpen())
    {
        // Force player body-part animations to Idle so the portrait doesn't
        // loop attack/hit animations while paused.
        for (auto pe : em.registry().view<PlayerActions>())
        {
            for (auto [child, bp, anim] : em.registry().view<BodyPart, Animation>().each())
            {
                if (bp.parent == pe && anim.state != AnimState::Idle)
                    anim.state = AnimState::Idle;
            }
            break;
        }
        handleMapRegen(engine, em);
        updateTitleBar(engine, em);
        return;
    }

    // Accumulate playtime while actively playing.
    em.registry().ctx().get<RunStats>().time += static_cast<float>(dt);

    // Detect WaveState transitions -> GameState transitions.
    if (waveState.phase == WaveState::Phase::GameOver)
    {
        gs.phase = GameState::Phase::GameOver;
        GameOverScreen::reset();
        AudioSystem::stopMusic();
        if (auto* t = em.registry().ctx().get<MusicConfig>().get("game_over"))
            AudioSystem::playSfx(t->path, t->volume);
        updateTitleBar(engine, em);
        engine.requestTimingReset();
        return;
    }
    if (waveState.phase == WaveState::Phase::Complete)
    {
        gs.phase = GameState::Phase::Victory;
        VictoryScreen::reset();
        updateTitleBar(engine, em);
        engine.requestTimingReset();
        return;
    }

    // Normal gameplay systems.
    PickupSystem::update(em);
    EquipmentSystem::update(em);
    WaveSystem::update(em, dt);
    CombatSystem::update(em, dt);
    AnimStateSystem::update(em);
    TintSystem::update(em, dt);
    AggroSystem::update(em);
    AmbientSoundSystem::update(em, dt);
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
    {
        static int sRecorderFrame = 0;
        AIRecorder::tick(em, sRecorderFrame++);
    }
    SteeringSystem::update(em, dt);
    MovementSystem::update(em, dt);
    CollisionSystem::update(em);
    DamageSystem::update(em);
    ProjectileSystem::update(em, static_cast<float>(dt));
    DeathSystem::update(em, dt);
    CraftingSystem::update(em);
    LevelingSystem::update(em);
    WeaponXPSystem::update(em);
    RestSpotSystem::update(em, dt);
    LadderSystem::update(em, dt);
    ParticleSystem::update(em, dt);
    CameraPanSystem::update(em, dt);
    CameraSystem::update(em);

    // Lock-on camera offset: blend camera toward the locked target.
    for (auto [entity, lockOn, transform, camera] :
         em.registry().view<LockOnTarget, Transform, Camera>().each())
    {
        if (!em.registry().valid(lockOn.target))
            continue;
        const auto& tt = em.registry().get<Transform>(lockOn.target);
        static constexpr float LOCK_ON_CAM_WEIGHT = 0.3f;
        static constexpr float CAM_BLEND = 0.1f;
        const float goalX = transform.x + (tt.x - transform.x) * LOCK_ON_CAM_WEIGHT;
        const float goalY = transform.y + (tt.y - transform.y) * LOCK_ON_CAM_WEIGHT;
        camera.x += (goalX - camera.x) * CAM_BLEND;
        camera.y += (goalY - camera.y) * CAM_BLEND;
    }

    handleMapRegen(engine, em);
    updateTitleBar(engine, em);
}

// Find the nearest alive enemy within range. Returns entt::null if none found.
static entt::entity findNearestEnemy(entt::registry& reg, float px, float py, float range)
{
    const float rangeSq = range * range;
    float bestSq = std::numeric_limits<float>::max();
    entt::entity best = entt::null;

    for (auto [e, ai, t] : reg.view<AIController, Transform>().each())
    {
        if (reg.all_of<Dead>(e))
            continue;
        const float dx = t.x - px;
        const float dy = t.y - py;
        const float dSq = dx * dx + dy * dy;
        if (dSq < bestSq && dSq <= rangeSq)
        {
            bestSq = dSq;
            best = e;
        }
    }
    return best;
}

// Handle lock-on toggle and per-frame validation. Returns the active LockOnTarget pointer
// (nullptr if no lock-on).
static LockOnTarget* updateLockOn(entt::registry& reg, entt::entity entity,
                                  const PlayerActions& actions, const Transform& transform,
                                  float lockOnRange)
{
    auto* lockOn = reg.try_get<LockOnTarget>(entity);

    if (actions.lock_on_toggle)
    {
        if (lockOn != nullptr)
        {
            reg.remove<LockOnTarget>(entity);
            return nullptr;
        }
        const entt::entity target = findNearestEnemy(reg, transform.x, transform.y, lockOnRange);
        if (target != entt::null)
        {
            reg.emplace<LockOnTarget>(entity, LockOnTarget{target});
            return &reg.get<LockOnTarget>(entity);
        }
        return nullptr;
    }

    if (lockOn == nullptr)
        return nullptr;

    // Validate each frame: dead, destroyed, or out of range → auto-switch or disengage.
    bool invalid = !reg.valid(lockOn->target) || reg.all_of<Dead>(lockOn->target);
    if (!invalid)
    {
        const auto& tt = reg.get<Transform>(lockOn->target);
        const float dx = tt.x - transform.x;
        const float dy = tt.y - transform.y;
        if (dx * dx + dy * dy > lockOnRange * lockOnRange)
            invalid = true;
    }
    if (!invalid)
        return lockOn;

    const entt::entity next = findNearestEnemy(reg, transform.x, transform.y, lockOnRange);
    if (next != entt::null)
    {
        lockOn->target = next;
        return lockOn;
    }
    reg.remove<LockOnTarget>(entity);
    return nullptr;
}

void gamePerFrame(Engine& engine, EntityManager& em, double /*dt*/)
{
    ZoneScopedN("gamePerFrame");

    const auto& gs = em.registry().ctx().get<GameState>();
    if (gs.phase != GameState::Phase::Playing)
        return;

    const auto& f = em.registry().ctx().get<FormulaConfig>();

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    static constexpr float RENDER_FACING_BLEND = 0.25f;
    for (auto [entity, actions, facing, transform] :
         em.registry().view<PlayerActions, FacingDirection, Transform>().each())
    {
        const LockOnTarget* lockOn =
            updateLockOn(em.registry(), entity, actions, transform, f.combat.lock_on_range);

        // Facing: lock-on overrides mouse aim.
        if (lockOn != nullptr)
        {
            const auto& tt = em.registry().get<Transform>(lockOn->target);
            const float dx = tt.x - transform.x;
            const float dy = tt.y - transform.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.0f)
            {
                facing.dx = dx / len;
                facing.dy = dy / len;
            }
        }
        else
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

void gameRenderDebug(Engine& engine, EntityManager& em)
{
    (void)engine;
    const auto& gs = em.registry().ctx().get<GameState>();
    if (gs.phase == GameState::Phase::Playing)
        AIDebugOverlay::render(em);
}

static void renderPlayingUI(Engine& engine, EntityManager& em, int ww, int wh, float frameDt)
{
    auto& ui = em.registry().ctx().get<UIState>();

    if (!ui.isScreenOpen())
    {
        HudRenderer::render(em, ww, wh);

        if (HudRenderer::renderMenuButton(em, ww, wh))
        {
            em.registry().ctx().get<UIState>().active_screen = UIState::Screen::Menu;
            PauseMenu::reset();
        }

        float camX = 0.0f;
        float camY = 0.0f;
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

    if (ui.active_screen == UIState::Screen::Menu)
    {
        const int menuResult = PauseMenu::render(em, ww, wh);
        if (menuResult == 1)
            engine.requestQuit();
        else if (menuResult == 2)
        {
            const auto& escSnd = em.registry().ctx().get<SoundConfig>().get("escape_run");
            if (!escSnd.path.empty())
                AudioSystem::playSfx(escSnd.path, escSnd.volume);
            ui.active_screen = UIState::Screen::None;
            transitionToSummary(em, true);
        }
    }
    else if (ui.active_screen == UIState::Screen::LevelUp)
        LevelUpScreen::render(em, ww, wh);
    else if (ui.active_screen == UIState::Screen::Sanctuary)
        SanctuaryScreen::render(em, ww, wh);
    else if (ui.active_screen == UIState::Screen::Crafting)
        CraftingScreen::render(em, ww, wh);

    // Render notifications last so they appear above all screen overlays.
    NotificationSystem::render(frameDt, ww, wh);
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    ZoneScopedN("gameRenderUI");

    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();
    auto& gs = em.registry().ctx().get<GameState>();
    const float frameDt = static_cast<float>(engine.lastFrameTime());

    // Show system cursor in all non-Playing states + when menu is open.
    const bool showCursor = (gs.phase != GameState::Phase::Playing) ||
                            em.registry().ctx().get<UIState>().isScreenOpen();
    SDL_ShowCursor(showCursor ? SDL_ENABLE : SDL_DISABLE);

    switch (gs.phase)
    {
    case GameState::Phase::MainMenu:
    {
        auto action = MainMenuScreen::render(em, ww, wh);
        switch (action)
        {
        case MainMenuScreen::Action::NewGame:
            CharCreateScreen::reset();
            gs.phase = GameState::Phase::CharCreate;
            break;
        case MainMenuScreen::Action::LoadGame:
            LoadGameScreen::reset();
            gs.phase = GameState::Phase::LoadGame;
            break;
        case MainMenuScreen::Action::HighScores:
            HighScoresScreen::reset();
            gs.phase = GameState::Phase::HighScores;
            break;
        case MainMenuScreen::Action::Settings:
            SettingsScreen::reset();
            gs.phase = GameState::Phase::Settings;
            break;
        case MainMenuScreen::Action::Quit:
            engine.requestQuit();
            break;
        case MainMenuScreen::Action::None:
            break;
        }
        break;
    }

    case GameState::Phase::CharCreate:
    {
        if (gs.pending_world_create)
        {
            // Loading screen: black background + "Wave 1" centered text.
            UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(ww), static_cast<float>(wh),
                                 {0.0f, 0.0f, 0.0f, 1.0f});
            const std::string loadText = "Wave 1";
            const auto lts = UIRenderer::measureText(sTitleFont, loadText);
            UIRenderer::drawText(sTitleFont, loadText, (static_cast<float>(ww) - lts.width) * 0.5f,
                                 (static_cast<float>(wh) - lts.height) * 0.5f,
                                 {1.0f, 0.85f, 0.3f, 1.0f});
            break;
        }
        auto action = CharCreateScreen::render(em, ww, wh);
        if (action == CharCreateScreen::Action::Start)
        {
            auto& saveData = em.registry().ctx().get<SaveData>();
            gs.active_character = CharCreateScreen::getName();
            SaveManager::addCharacter(saveData, gs.active_character);
            SaveManager::save(saveData);
            AudioSystem::stopMusic();
            auto& ui = em.registry().ctx().get<UIState>();
            ui = UIState{};
            ui.input_suppressed = true;
            gs.pending_world_create = true;
        }
        else if (action == CharCreateScreen::Action::Back)
        {
            MainMenuScreen::reset();
            gs.phase = GameState::Phase::MainMenu;
        }
        break;
    }

    case GameState::Phase::LoadGame:
    {
        if (gs.pending_world_create)
        {
            UIRenderer::drawRect(0.0f, 0.0f, static_cast<float>(ww), static_cast<float>(wh),
                                 {0.0f, 0.0f, 0.0f, 1.0f});
            const std::string loadText = "Wave 1";
            const auto lts = UIRenderer::measureText(sTitleFont, loadText);
            UIRenderer::drawText(sTitleFont, loadText, (static_cast<float>(ww) - lts.width) * 0.5f,
                                 (static_cast<float>(wh) - lts.height) * 0.5f,
                                 {1.0f, 0.85f, 0.3f, 1.0f});
            break;
        }
        auto action = LoadGameScreen::render(em, ww, wh);
        if (action == LoadGameScreen::Action::Select)
        {
            gs.active_character = LoadGameScreen::getSelectedName();
            AudioSystem::stopMusic();
            auto& ui = em.registry().ctx().get<UIState>();
            ui = UIState{};
            ui.input_suppressed = true;
            gs.pending_world_create = true;
        }
        else if (action == LoadGameScreen::Action::Back)
        {
            MainMenuScreen::reset();
            gs.phase = GameState::Phase::MainMenu;
        }
        break;
    }

    case GameState::Phase::Playing:
        renderPlayingUI(engine, em, ww, wh, frameDt);
        break;

    case GameState::Phase::GameOver:
    {
        if (GameOverScreen::render(em, ww, wh, frameDt))
        {
            transitionToSummary(em, false);
            engine.requestTimingReset();
        }
        break;
    }

    case GameState::Phase::Victory:
    {
        if (VictoryScreen::render(em, ww, wh, frameDt))
        {
            transitionToSummary(em, true);
            engine.requestTimingReset();
        }
        break;
    }

    case GameState::Phase::RunSummary:
    {
        if (RunSummaryScreen::render(em, ww, wh))
        {
            WorldInit::destroyWorld(em);
            engine.requestTimingReset();
            MainMenuScreen::reset();
            gs.phase = GameState::Phase::MainMenu;
            playMainMenuMusic(em);
        }
        break;
    }

    case GameState::Phase::HighScores:
    {
        if (HighScoresScreen::render(em, ww, wh))
        {
            MainMenuScreen::reset();
            gs.phase = GameState::Phase::MainMenu;
            // Menu music keeps playing (never stopped for HighScores).
        }
        break;
    }

    case GameState::Phase::Settings:
    {
        SettingsScreen::render(em, ww, wh);
        // SettingsScreen handles its own back-to-MainMenu transition.
        break;
    }
    }

    // Debug overlay renders over everything in all states.
    DebugOverlay::render(engine, em, ww, wh);

    // Clear event buffers after all UI screens have consumed them.
    em.key_down_events.clear();
    em.mouse_down_events.clear();
    em.mouse_wheel_y = 0;
    em.text_input_buffer.clear();
}
