#pragma once

#include "SpriteCompositor.h"
#include "TextureManager.h"
#include "ecs/EntityManager.h"

#include <string>

// Forward declaration — avoids pulling SDL2 headers into every file that includes Engine.h.
// Only Engine.cpp needs to know the internals of SDL_Window.
struct SDL_Window;

class Engine
{
  public:
    Engine();
    ~Engine();

    bool init(const char* title, int width, int height);
    void run();
    void shutdown();

    // Game-side logic callback. Called once per fixed-step tick.
    // Engine passes itself so the game can call setWindowTitle() / lastFrameTime().
    using GameUpdateFn = void (*)(Engine&, EntityManager&, double);
    void setGameUpdate(GameUpdateFn fn);

    // Per-frame callback. Called once per render frame after SDL event polling,
    // before the fixed-step loop. Use for input that must track the display rate
    // (e.g. mouse-aim facing) rather than the fixed tick rate.
    using PerFrameFn = void (*)(Engine&, EntityManager&, double);
    void setPerFrameUpdate(PerFrameFn fn);

    // Pre-render callback. Called once per frame after the fixed-step loop
    // and after render_alpha is computed, but before rendering. Use for any
    // per-frame state that needs the final render_alpha (e.g. interpolating
    // aim override positions to match sprite interpolation).
    using PreRenderFn = void (*)(Engine&, EntityManager&);
    void setPreRender(PreRenderFn fn);

    // Debug render callback. Called once per frame after world rendering,
    // between UIRenderer::beginFrame() and the UI render callback.
    // DebugDraw::setCamera() is called automatically before this callback.
    // Game code uses DebugDraw calls here to render world-space debug info.
    using RenderDebugFn = void (*)(Engine&, EntityManager&);
    void setRenderDebug(RenderDebugFn fn);

    // UI render callback. Called once per frame after world rendering, between
    // UIRenderer::beginFrame() and UIRenderer::endFrame(). Game code uses
    // UIRenderer draw calls here to render HUD, menus, notifications, etc.
    using RenderUIFn = void (*)(Engine&, EntityManager&);
    void setRenderUI(RenderUIFn fn);

    // Set the window title string (for game-side HUD display).
    void setWindowTitle(const std::string& title);

    // Request the engine to stop running (used by pause menu Quit option).
    void requestQuit()
    {
        running = false;
    }

    int windowWidth() const
    {
        return window_w;
    }
    int windowHeight() const
    {
        return window_h;
    }

    void setCameraZoom(float zoom)
    {
        camera_zoom = zoom;
    }
    float cameraZoom() const
    {
        return camera_zoom;
    }

    // EMA-smoothed frame time for FPS calculation.
    double lastFrameTime() const
    {
        return last_frame_time;
    }

    // Exposed so game code (main.cpp, future scene managers) can create
    // entities and attach components before calling run().
    EntityManager& entityManager()
    {
        return entity_manager;
    }

    TextureManager& textureManager()
    {
        return texture_manager;
    }

    SpriteCompositor& spriteCompositor()
    {
        return sprite_compositor;
    }

    // Push the current back buffer to the display. Used by game code to show
    // a loading overlay before a heavy synchronous operation (map regen).
    void swapBuffers();

    // After a heavy synchronous operation (map gen), reset the frame timer
    // and accumulator so the FPS counter doesn't crater and no catch-up ticks fire.
    void requestTimingReset()
    {
        timing_reset_pending = true;
    }

  private:
    void processEvents();
    void update(double dt);
    void render();

    SDL_Window* window = nullptr;
    void* gl_context = nullptr;
    bool running = false;
    int window_w = 0;
    int window_h = 0;

    EntityManager entity_manager;
    TextureManager texture_manager;
    SpriteCompositor sprite_compositor;
    double last_frame_time = 1.0 / 60.0; // seconds; used for title-bar FPS display
    double frame_dt = 1.0 / 60.0;        // raw wall-clock frame time for animation

    GameUpdateFn game_update = nullptr;
    PerFrameFn per_frame_update = nullptr;
    PreRenderFn pre_render = nullptr;
    RenderDebugFn render_debug = nullptr;
    RenderUIFn render_ui = nullptr;
    float camera_zoom = 1.0f;
    bool timing_reset_pending = false;
};
