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

        if (frameTime > MAX_FRAME_TIME)
            frameTime = MAX_FRAME_TIME;

        accumulator += frameTime;

        processEvents();

        // Fixed-rate update — always steps in 1/60s increments.
        while (accumulator >= FIXED_TIMESTEP)
        {
            update(FIXED_TIMESTEP);
            accumulator -= FIXED_TIMESTEP;
        }

        render();

        // Tracy frame marker — marks the end of one complete frame.
        // When Tracy is disabled (TRACY_ENABLE=OFF) this compiles to nothing.
        FrameMark;
    }
}

void Engine::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            running = false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            running = false;
    }

    // InputSystem reads the keyboard state snapshot that SDL_PollEvent just refreshed.
    // Must be called after the event loop, not inside the fixed-step update.
    InputSystem::update(entity_manager);
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
        std::string title =
            "Hell Escape"
            "  |  HP " +
            std::to_string(health.current) + "/" + std::to_string(health.max) + "  |  LVL " +
            std::to_string(exp.level) + "  XP " + std::to_string(exp.current_xp) + "/" +
            std::to_string(exp.xp_to_next) + "  |  STR " + std::to_string(stats.str) + "  DEX " +
            std::to_string(stats.dex) + "  END " + std::to_string(stats.end) + "  LCK " +
            std::to_string(stats.lck) + "  pts " + std::to_string(exp.stat_points);
        SDL_SetWindowTitle(window, title.c_str());
        break;
    }
}

void Engine::render()
{
    ZoneScoped; // Tracy zone — visible in the profiler as "render"

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Find the active camera position. Default to window centre if none exists.
    float camX = static_cast<float>(window_w) * 0.5f;
    float camY = static_cast<float>(window_h) * 0.5f;
    for (auto [entity, camera] : entity_manager.registry().view<Camera>().each())
    {
        if (camera.active)
        {
            camX = camera.x;
            camY = camera.y;
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
