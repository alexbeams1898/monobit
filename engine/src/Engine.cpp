#include "Engine.h"

#include "FontManager.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "systems/AnimationSystem.h"
#include "systems/AudioSystem.h"
#include "systems/RenderSystem.h"
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
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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
    FontManager::init();
    UIRenderer::init(window_w, window_h);
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
        frame_dt = frameTime; // raw wall-clock dt for animation timing
        if (frameTime > MAX_FRAME_TIME)
            frameTime = MAX_FRAME_TIME;

        accumulator += frameTime;

        processEvents();

        if (per_frame_update)
            per_frame_update(*this, entity_manager, frame_dt);

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
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
        {
            window_w = event.window.data1;
            window_h = event.window.data2;
            RenderSystem::resize(window_w, window_h);
            UIRenderer::resize(window_w, window_h);
        }

        // Buffer one-shot input events so they survive across fixed-step ticks.
        // Without this, a brief key tap between two ticks is lost because
        // SDL_GetKeyboardState shows the key already released.
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
            entity_manager.key_down_events.push_back(event.key.keysym.scancode);
        if (event.type == SDL_MOUSEBUTTONDOWN)
            entity_manager.mouse_down_events.push_back(event.button.button);
        if (event.type == SDL_TEXTINPUT)
            entity_manager.text_input_buffer += event.text.text;
    }
}

void Engine::update(double dt)
{
    ZoneScoped;

    if (game_update)
        game_update(*this, entity_manager, dt);

    // Sync body-part children to their parent's position.
    // Must run after all movement/correction systems so children have the
    // final parent position before the next PreviousTransform snapshot.
    for (auto [child, bp, t] : entity_manager.registry().view<BodyPart, Transform>().each())
    {
        if (entity_manager.registry().valid(bp.parent))
        {
            const auto& pt = entity_manager.registry().get<Transform>(bp.parent);
            t.x = pt.x;
            t.y = pt.y;
        }
    }
}

void Engine::setGameUpdate(GameUpdateFn fn)
{
    game_update = fn;
}

void Engine::setPerFrameUpdate(PerFrameFn fn)
{
    per_frame_update = fn;
}

void Engine::setRenderUI(RenderUIFn fn)
{
    render_ui = fn;
}

void Engine::setWindowTitle(const std::string& title)
{
    if (window)
        SDL_SetWindowTitle(window, title.c_str());
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

    // Sync body-part positions before animation (covers first-frame edge case
    // where no tick has run yet but we're about to render).
    for (auto [child, bp, t] : entity_manager.registry().view<BodyPart, Transform>().each())
    {
        if (entity_manager.registry().valid(bp.parent))
        {
            const auto& pt = entity_manager.registry().get<Transform>(bp.parent);
            t.x = pt.x;
            t.y = pt.y;
        }
    }

    AnimationSystem::update(entity_manager, static_cast<float>(frame_dt));
    TileMapRenderer::render(camX, camY, window_w, window_h);
    RenderSystem::render(entity_manager, texture_manager, camX, camY);

    // UI layer: screen-space overlay drawn after world content.
    UIRenderer::beginFrame();
    if (render_ui)
        render_ui(*this, entity_manager);
    UIRenderer::endFrame();

    SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
    AudioSystem::shutdown();
    UIRenderer::shutdown();
    FontManager::shutdown();
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
