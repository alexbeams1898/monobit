#include "Engine.h"

#include "ecs/Components.h"
#include "systems/CameraSystem.h"
#include "systems/InputSystem.h"
#include "systems/MovementSystem.h"
#include "systems/RenderSystem.h"

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

    glContext = SDL_GL_CreateContext(window);
    if (!glContext)
        return false;

    // Load all OpenGL 3.3 core function pointers via GLAD.
    // SDL_GL_GetProcAddress is the cross-platform way to retrieve them —
    // on Windows, opengl32.dll only exposes GL 1.1; the driver fills in the rest.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
    {
        SDL_GL_DeleteContext(glContext);
        glContext = nullptr;
        return false;
    }

    // Vsync on by default — will become a user setting in the options menu.
    SDL_GL_SetSwapInterval(1);

    windowW_ = width;
    windowH_ = height;

    RenderSystem::init(windowW_, windowH_);

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
    InputSystem::update(entityManager_);
}

void Engine::update(double dt)
{
    ZoneScoped; // Tracy zone — visible in the profiler as "update"
    MovementSystem::update(entityManager_, dt);
    CameraSystem::update(entityManager_); // must run after movement so camera snaps to new position
}

void Engine::render()
{
    ZoneScoped; // Tracy zone — visible in the profiler as "render"

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Find the active camera position. Default to window centre if none exists.
    float camX = static_cast<float>(windowW_) * 0.5f;
    float camY = static_cast<float>(windowH_) * 0.5f;
    for (auto [entity, camera] : entityManager_.registry().view<Camera>().each())
    {
        if (camera.active)
        {
            camX = camera.x;
            camY = camera.y;
            break;
        }
    }

    RenderSystem::render(entityManager_, textureManager_, camX, camY);

    SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
    RenderSystem::shutdown();
    textureManager_.clear();

    if (glContext)
    {
        SDL_GL_DeleteContext(glContext);
        glContext = nullptr;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
    running = false;
}
