#include "Engine.h"

#include "FontManager.h"
#include "UIRenderer.h"
#include "ecs/Components.h"
#include "systems/AnimationSystem.h"
#include "systems/AudioSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"
#include "utils/DebugDraw.h"

#include <cmath>
#include <cstdio>
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

        // Reset per-frame tick counter so render UI can detect 0-tick frames
        // and avoid clearing one-shot input buffers that no consumer saw yet.
        entity_manager.ticks_this_frame = 0;

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
            for (auto [entity, camera] : entity_manager.registry().view<Camera>().each())
            {
                camera.prev_x = camera.x;
                camera.prev_y = camera.y;
                camera.prev_offset_x = camera.offset_x;
                camera.prev_offset_y = camera.offset_y;
            }
            update(FIXED_TIMESTEP);
            accumulator -= FIXED_TIMESTEP;
            ++entity_manager.ticks_this_frame;

            // After a heavy synchronous operation (map gen), snap the clock
            // forward so no catch-up ticks fire and the FPS counter stays clean.
            if (timing_reset_pending)
            {
                timing_reset_pending = false;
                previousTime = static_cast<double>(SDL_GetTicks64()) / 1000.0;
                accumulator = 0.0;
                last_frame_time = 1.0 / 60.0;
                break;
            }
        }

        entity_manager.render_alpha = static_cast<float>(accumulator / FIXED_TIMESTEP);
        // Animation advances at wall-clock rate (not fixed-step) so sprites
        // interpolate smoothly on high-refresh displays. Run it before the
        // pre_render hook so game-side visual-sync systems can read fresh
        // sprite.src_x/flip_x values computed from the character's animation.
        AnimationSystem::update(entity_manager, static_cast<float>(frame_dt));
        if (pre_render)
            pre_render(*this, entity_manager);
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
        if (event.type == SDL_MOUSEWHEEL)
            entity_manager.mouse_wheel_y += event.wheel.y;
        if (event.type == SDL_TEXTINPUT)
            entity_manager.text_input_buffer += event.text.text;
    }
}

void Engine::update(double dt)
{
    ZoneScoped;

    if (game_update)
        game_update(*this, entity_manager, dt);
}

void Engine::setGameUpdate(GameUpdateFn fn)
{
    game_update = fn;
}

void Engine::setPerFrameUpdate(PerFrameFn fn)
{
    per_frame_update = fn;
}

void Engine::setPreRender(PreRenderFn fn)
{
    pre_render = fn;
}

void Engine::setRenderDebug(RenderDebugFn fn)
{
    render_debug = fn;
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

    glClearColor(clear_r, clear_g, clear_b, 1.0f);
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
            // Interpolate base position and offset separately. Both use the
            // same alpha so they share the interpolation base — no step-size
            // mismatch between camera and player sprite positions.
            if (!entity_manager.registry().all_of<CameraPan>(entity))
            {
                const float baseX = camera.prev_x + (camera.x - camera.prev_x) * a;
                const float baseY = camera.prev_y + (camera.y - camera.prev_y) * a;
                const float offX =
                    camera.prev_offset_x + (camera.offset_x - camera.prev_offset_x) * a;
                const float offY =
                    camera.prev_offset_y + (camera.offset_y - camera.prev_offset_y) * a;
                // Round the offset so it lands on integer pixels. Without this,
                // the sprite (rounded) and camera (rounded) have independent
                // rounding errors that don't cancel, causing 1-2px oscillation.
                camX = baseX + std::round(offX);
                camY = baseY + std::round(offY);
            }
            else
            {
                camX = camera.x;
                camY = camera.y;
            }
            break;
        }
    }

    // Per-frame camera/player position CSV for jitter diagnosis.
    // Only active in Tracy-enabled builds.
#ifdef TRACY_ENABLE
    {
        static FILE* jitterLog = nullptr;
        static int jitterFrame = 0;
        if (!jitterLog)
        {
            jitterLog = std::fopen("jitter.csv", "w");
            if (jitterLog)
                std::fprintf(jitterLog, "frame,alpha,camX,camY,"
                                        "offsetX,offsetY,prevOffsetX,prevOffsetY,"
                                        "drawX,drawY,"
                                        "screenX,screenY,frameDtMs\n");
        }
        if (jitterLog)
        {
            for (auto [pe, cam] : entity_manager.registry().view<Camera>().each())
            {
                if (!cam.active)
                    continue;
                const auto& tf = entity_manager.registry().get<Transform>(pe);
                float drawX = tf.x;
                float drawY = tf.y;
                if (const auto* prev = entity_manager.registry().try_get<PreviousTransform>(pe))
                {
                    drawX = prev->x + (tf.x - prev->x) * a;
                    drawY = prev->y + (tf.y - prev->y) * a;
                }
                std::fprintf(jitterLog,
                             "%d,%.4f,%.4f,%.4f,"
                             "%.4f,%.4f,%.4f,%.4f,"
                             "%.4f,%.4f,"
                             "%.4f,%.4f,%.3f\n",
                             jitterFrame, a, camX, camY, cam.offset_x, cam.offset_y,
                             cam.prev_offset_x, cam.prev_offset_y, drawX, drawY, drawX - camX,
                             drawY - camY, frame_dt * 1000.0);
                break;
            }
            jitterFrame++;
            std::fflush(jitterLog);
        }
    }
#endif

    TileMapRenderer::render(camX, camY, window_w, window_h, camera_zoom);
    RenderSystem::render(entity_manager, texture_manager, camX, camY, camera_zoom);

    // UI layer: screen-space overlay drawn after world content.
    UIRenderer::beginFrame();
    DebugDraw::setCamera(camX, camY, window_w, window_h, camera_zoom);
    if (render_debug)
        render_debug(*this, entity_manager);
    if (render_ui)
        render_ui(*this, entity_manager);
    UIRenderer::endFrame();

    SDL_GL_SwapWindow(window);
}

void Engine::swapBuffers()
{
    if (window)
        SDL_GL_SwapWindow(window);
}

void Engine::shutdown()
{
    AudioSystem::shutdown();
    UIRenderer::shutdown();
    FontManager::shutdown();
    TileMapRenderer::shutdown();
    RenderSystem::shutdown();
    sprite_compositor.clear();
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
