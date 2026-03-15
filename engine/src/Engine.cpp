#include "Engine.h"
#include <SDL.h>
#include <SDL_opengl.h>

// Fixed-timestep constants.
// Update runs at a locked 60 Hz regardless of render frame rate.
// MAX_FRAME_TIME caps the catchup window — prevents the "spiral of death"
// where a slow frame causes even more updates, causing even slower frames.
static constexpr double FIXED_TIMESTEP = 1.0 / 60.0;
static constexpr double MAX_FRAME_TIME = 0.25;

Engine::Engine()  = default;
Engine::~Engine() { shutdown(); }

bool Engine::init(const char* title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return false;

    // Request an OpenGL 3.3 core context.
    // "Core" means deprecated legacy features are removed — keeps things clean.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) return false;

    glContext = SDL_GL_CreateContext(window);
    if (!glContext) return false;

    // 0 = uncapped render rate — we control timing via the fixed timestep loop.
    // Set to 1 to enable vsync later if needed.
    SDL_GL_SetSwapInterval(0);

    return true;
}

void Engine::run()
{
    running = true;
    double previousTime = SDL_GetTicks64() / 1000.0;
    double accumulator  = 0.0;

    while (running) {
        const double currentTime = SDL_GetTicks64() / 1000.0;
        double frameTime         = currentTime - previousTime;
        previousTime             = currentTime;

        if (frameTime > MAX_FRAME_TIME)
            frameTime = MAX_FRAME_TIME;

        accumulator += frameTime;

        processEvents();

        // Fixed-rate update — always steps in 1/60s increments
        while (accumulator >= FIXED_TIMESTEP) {
            update(FIXED_TIMESTEP);
            accumulator -= FIXED_TIMESTEP;
        }

        render();
    }
}

void Engine::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            running = false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            running = false;
    }
}

void Engine::update(double dt)
{
    (void)dt; // suppress unused warning — systems plugged in here from Issue #3 onward
}

void Engine::render()
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Render systems plugged in here from Issue #6 onward

    SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
    if (glContext) {
        SDL_GL_DeleteContext(glContext);
        glContext = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
    running = false;
}
