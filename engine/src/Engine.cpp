#include "Engine.h"

#include "systems/InputSystem.h"
#include "systems/MovementSystem.h"
#include "systems/RenderSystem.h"

#include <SDL.h>
#include <SDL_opengl.h>

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

    // OpenGL 2.1 compatibility profile — required by the stub RenderSystem which
    // uses legacy glBegin/glOrtho calls. Issue #6 switches this to 3.3 core profile
    // once GLAD is added and real VAO/VBO/shader rendering is in place.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window)
        return false;

    glContext = SDL_GL_CreateContext(window);
    if (!glContext)
        return false;

    // Vsync on by default — will become a user setting in the options menu.
    SDL_GL_SetSwapInterval(1);

    windowW_ = width;
    windowH_ = height;

    // One-time projection setup for the stub renderer.
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

        // Fixed-rate update — always steps in 1/60s increments
        while (accumulator >= FIXED_TIMESTEP)
        {
            update(FIXED_TIMESTEP);
            accumulator -= FIXED_TIMESTEP;
        }

        render();
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
    MovementSystem::update(entityManager_, dt);
}

void Engine::render()
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    RenderSystem::render(entityManager_);

    SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
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
