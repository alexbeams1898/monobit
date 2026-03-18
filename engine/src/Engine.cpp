#include "Engine.h"

#include "ecs/Components.h"
#include "systems/AggroSystem.h"
#include "systems/AudioSystem.h"
#include "systems/CameraSystem.h"
#include "systems/ChaseSystem.h"
#include "systems/CollisionSystem.h"
#include "systems/CombatSystem.h"
#include "systems/DamageSystem.h"
#include "systems/DeathSystem.h"
#include "systems/FlowFieldSystem.h"
#include "systems/InputSystem.h"
#include "systems/LevelingSystem.h"
#include "systems/MovementSystem.h"
#include "systems/PickupSystem.h"
#include "systems/RenderSystem.h"
#include "systems/RestSpotSystem.h"
#include "systems/SpawnerSystem.h"
#include "systems/SteeringSystem.h"
#include "systems/TileMapRenderer.h"

#include <cmath>
#include <string>

// glad must be included before any SDL OpenGL header.
#include <SDL.h>
#include <glad/glad.h>
#include <tracy/Tracy.hpp>

// Fixed-timestep constants.
// Update runs at a locked 60 Hz regardless of render frame rate.
// MAX_FRAME_TIME caps the catchup window — prevents the "spiral of death"
// where a slow frame causes even more updates, causing even slower frames.
static constexpr double FIXED_TIMESTEP = 1.0 / 60.0;
static constexpr double MAX_FRAME_TIME = 0.25;

Engine::Engine() = default;
Engine::~Engine()
{
    shutdown();
}

bool Engine::init(const char* title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return false;

    // OpenGL 3.3 core profile — modern rendering without legacy cruft.
    // Core profile removes deprecated features (glBegin, glOrtho, etc.)
    // and requires explicit VAO/VBO/shader usage.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window)
        return false;

    SDL_ShowCursor(SDL_DISABLE);

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        return false;

    // Load all OpenGL 3.3 core function pointers via GLAD.
    // SDL_GL_GetProcAddress is the cross-platform way to retrieve them —
    // on Windows, opengl32.dll only exposes GL 1.1; the driver fills in the rest.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
    {
        SDL_GL_DeleteContext(gl_context);
        gl_context = nullptr;
        return false;
    }

    // Vsync on by default — will become a user setting in the options menu.
    SDL_GL_SetSwapInterval(1);

    window_w = width;
    window_h = height;

    RenderSystem::init(window_w, window_h);
    TileMapRenderer::init();
    AudioSystem::init(); // non-fatal — game runs without audio if device unavailable

    return true;
}

void Engine::run()
{
    running = true;
    double previousTime = static_cast<double>(SDL_GetTicks64()) / 1000.0;
    double accumulator = 0.0;

    while (running)
    {
        const double currentTime = static_cast<double>(SDL_GetTicks64()) / 1000.0;
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        last_frame_time = last_frame_time * 0.97 + frameTime * 0.03; // EMA smoothing
        if (frameTime > MAX_FRAME_TIME)
            frameTime = MAX_FRAME_TIME;

        accumulator += frameTime;

        processEvents();

        // Fixed-rate update — always steps in 1/60s increments.
        while (accumulator >= FIXED_TIMESTEP)
        {
            // Snapshot positions before this tick for render interpolation.
            for (auto [entity, transform] : entity_manager.registry().view<Transform>().each())
            {
                auto& prev = entity_manager.registry().get_or_emplace<PreviousTransform>(entity);
                prev.x = transform.x;
                prev.y = transform.y;
            }
            update(FIXED_TIMESTEP);
            accumulator -= FIXED_TIMESTEP;
        }

        entity_manager.render_alpha = static_cast<float>(accumulator / FIXED_TIMESTEP);
        render();

        // Tracy frame marker — marks the end of one complete frame.
        // When Tracy is disabled (TRACY_ENABLE=OFF) this compiles to nothing.
        FrameMark;
    }
}

void Engine::processEvents()
{
    bool focus_lost = false;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            running = false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            focus_lost = true;
    }

    // InputSystem reads the keyboard state snapshot that SDL_PollEvent just refreshed.
    // Must be called after the event loop, not inside the fixed-step update.
    InputSystem::update(entity_manager, window_w, window_h);

    // If the window lost focus this frame (Alt-Tab, controller/keyboard disconnect, etc.)
    // zero out all inputs so the player doesn't keep sliding.
    // Note: this covers OS-level focus loss. A keyboard that physically dies while the
    // window remains focused won't trigger this — see GitHub issue #34 for that edge case.
    if (focus_lost)
    {
        for (auto [entity, inp] : entity_manager.registry().view<Input>().each())
        {
            inp.move_x = 0.0f;
            inp.move_y = 0.0f;
            inp.attack = false;
            inp.dodge = false;
            inp.sprint = false;
            inp.skill = false;
            inp.block_held = false;
            inp.block_just_pressed = false;
        }
    }

    // Player facing: derived from mouse position (per-frame input), not physics.
    // Updated here instead of in the fixed-step loop so facing always reflects the
    // current mouse position — avoids stale-facing wobble on 0-update frames.
    //
    // render_dx/dy blends toward dx/dy for smooth visual rotation (dot, future
    // sprite direction). Gameplay reads dx/dy directly for instant combat response.
    // Blend factor 0.25 at 60fps ≈ 98% converged in 200ms — responsive and smooth.
    static constexpr float RENDER_FACING_BLEND = 0.25f;
    for (auto [entity, input, facing] :
         entity_manager.registry().view<Input, FacingDirection>().each())
    {
        if (entity_manager.registry().all_of<Dodging>(entity))
            continue;
        const float len = std::sqrt(input.last_facing_x * input.last_facing_x +
                                    input.last_facing_y * input.last_facing_y);
        if (len > 0.0f)
        {
            facing.dx = input.last_facing_x / len;
            facing.dy = input.last_facing_y / len;
        }

        // Smooth visual facing toward gameplay facing.
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

// 0.0 = no scaling → "-"; otherwise first threshold the value meets (s → e).
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

void Engine::update(double dt)
{
    ZoneScoped;
    // InputSystem runs in processEvents() before the fixed-step loop —
    // see processEvents() for the call site.  The order here is the
    // per-tick combat/movement/collision sequence.
    SpawnerSystem::update(entity_manager, dt); // timed wave spawner — enemies from outside bounds
    CombatSystem::update(entity_manager, dt);  // cooldowns, hitbox spawn, dodge, skill, auto-attack
    AggroSystem::update(entity_manager);       // Idle→Chase when player enters aggro radius
    FlowFieldSystem::update(entity_manager);   // BFS from player — rebuilds only on cell change
    ChaseSystem::update(entity_manager, dt);   // enemies read flow field → write velocity
    SteeringSystem::update(entity_manager); // wall repulsion — deflects velocity before integration
    MovementSystem::update(entity_manager, dt); // DEX-scaled speed, skip Dodging, FacingDirection
    CollisionSystem::update(entity_manager);    // dynamic-vs-dynamic correction + events
    DamageSystem::update(entity_manager);       // hitbox→health, enemy→player, shield/parry
    DeathSystem::update(entity_manager);        // spawn XP pickups, destroy Dead entities
    PickupSystem::update(entity_manager);       // auto-collect XP/money within radius
    LevelingSystem::update(entity_manager);     // XP overflow → level up → stat points
    RestSpotSystem::update(entity_manager, dt); // heal player to full when standing on rest spot
    CameraSystem::update(entity_manager);       // snap camera to final player position

    // Title-bar HUD — cheapest possible stat display, no font rendering needed.
    for (auto [entity, input, health, stats, exp] :
         entity_manager.registry().view<Input, Health, Stats, Experience>().each())
    {
        const auto& f = entity_manager.formulas;

        // Computed attack — base_damage + stat scaling from equipped weapon.
        int atk = 5; // fist baseline
        std::string weaponGrade;
        if (entity_manager.registry().all_of<Weapon>(entity))
        {
            const auto& w = entity_manager.registry().get<Weapon>(entity);
            atk = static_cast<int>(computeDamage(w, stats, f));

            const std::string& wname = w.name.empty() ? std::string("?") : w.name;
            weaponGrade = "  ---  " + wname + "  str " + gradeChar(w.str_scaling, f) + " / dex " +
                          gradeChar(w.dex_scaling, f);
        }

        // Computed defense — mirrors DamageSystem::computeDef formula.
        const int def = static_cast<int>(std::min(
            f.defense.cap, std::floor(static_cast<float>(stats.str) * f.defense.str_scale +
                                      static_cast<float>(stats.end) * f.defense.end_scale +
                                      static_cast<float>(exp.level) * f.defense.level_scale)));

        // Poise threshold (0 = staggers on any hit).
        const int poise = entity_manager.registry().all_of<Poise>(entity)
                              ? static_cast<int>(entity_manager.registry().get<Poise>(entity).max)
                              : 0;

        const int fps = static_cast<int>(std::lround(1.0 / last_frame_time));
        std::string title =
            "Hell Escape"
            "  |  FPS " +
            std::to_string(fps) + "/60  |  HP " + std::to_string(health.current) + "/" +
            std::to_string(health.max) + "  |  LVL " + std::to_string(exp.level) + "  XP " +
            std::to_string(exp.current_xp) + "/" + std::to_string(exp.xp_to_next) + "  |  STR " +
            std::to_string(stats.str) + "  DEX " + std::to_string(stats.dex) + "  END " +
            std::to_string(stats.end) + "  LCK " + std::to_string(stats.lck) + "  pts " +
            std::to_string(exp.stat_points) + "  |  ATK " + std::to_string(atk) + "  DEF " +
            std::to_string(def) + "  POISE " + std::to_string(poise) + weaponGrade;
        SDL_SetWindowTitle(window, title.c_str());
        break;
    }
}

void Engine::render()
{
    ZoneScoped; // Tracy zone — visible in the profiler as "render"

    const float a = entity_manager.render_alpha;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Find the active camera position, interpolated between previous and current
    // tick using the accumulator remainder (alpha). This eliminates the visual
    // wobble caused by rendering at stale positions between fixed-rate updates.
    float camX = static_cast<float>(window_w) * 0.5f;
    float camY = static_cast<float>(window_h) * 0.5f;
    for (auto [entity, camera] : entity_manager.registry().view<Camera>().each())
    {
        if (camera.active)
        {
            const auto* prev = entity_manager.registry().try_get<PreviousTransform>(entity);
            if (prev)
            {
                camX = prev->x + (camera.x - prev->x) * a;
                camY = prev->y + (camera.y - prev->y) * a;
            }
            else
            {
                camX = camera.x;
                camY = camera.y;
            }
            break;
        }
    }

    TileMapRenderer::render(camX, camY, window_w, window_h);
    RenderSystem::render(entity_manager, texture_manager, camX, camY);

    SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
    AudioSystem::shutdown();
    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    texture_manager.clear();

    if (gl_context)
    {
        SDL_GL_DeleteContext(gl_context);
        gl_context = nullptr;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
    running = false;
}
