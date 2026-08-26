#pragma once

#include "SpriteCompositor.h"
#include "TextureManager.h"
#include "ecs/EntityManager.h"

#include <string>

// Forward declaration — avoids pulling SDL2 headers into every file that includes Engine.h.
// Only Engine.cpp needs the internals of these.
struct SDL_Window;
union SDL_Event;

class Engine
{
  public:
    Engine();
    ~Engine();

    bool init(const char* title, int width, int height);
    void run();
    void shutdown();

    // Multi-sample anti-aliasing on the default framebuffer. Must be set
    // BEFORE init() — the value is consumed when the window is created.
    // Pass 0 to disable (default), or a power-of-two sample count (2, 4, 8).
    // Higher = smoother edges + lower performance. 4 is the standard
    // quality/cost trade for 3D games. 2D games typically pass 0.
    void setMSAA(int samples)
    {
        msaa_samples = samples;
    }

    // Window startup mode. Must be set BEFORE init() (the value is consumed
    // when the window is created). Defaults to Windowed at the requested
    // width/height.
    enum class WindowMode
    {
        // Standard window at the size passed to init(). User can resize.
        Windowed,
        // Maximized window — full monitor area but keeps title bar / borders
        // / resize handles. The width/height passed to init() are used as
        // the restore size when the user un-maximizes.
        Maximized,
        // Borderless fullscreen at the desktop's native resolution. No
        // title bar. Width/height passed to init() are ignored — actual
        // size is read from the display.
        BorderlessFullscreen,
    };
    void setWindowMode(WindowMode mode)
    {
        window_mode = mode;
    }

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

    // World render callback. Called once per frame after the framebuffer is
    // cleared and the active 2D Camera is interpolated, but before the UI
    // pass. The game owns world rendering: 2D games call TileMapRenderer +
    // RenderSystem here; 3D games run their own pipeline (geometry, lighting,
    // post-process). camX/camY/alpha are provided for games that use the 2D
    // Camera component and can be ignored otherwise.
    using RenderWorldFn = void (*)(Engine&, EntityManager&, float camX, float camY, float alpha);
    void setRenderWorld(RenderWorldFn fn);

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

    // ImGui render callback. Called after the UI pass; the engine has
    // already done ImGui::NewFrame for this frame, so the game just calls
    // ImGui::Begin/Sliders/End. Engine handles input forwarding and final
    // ImGui::Render automatically. Used for in-game tuning overlays and
    // dev panels — toggle visibility from inside the callback (e.g.
    // gate Begin() on a global "show panel" bool the game owns).
    using RenderImGuiFn = void (*)(Engine&, EntityManager&);
    void setRenderImGui(RenderImGuiFn fn);

    // Resize callback. Called from the SDL window-resize event handler after
    // the engine updates window_w/window_h and resizes UIRenderer (the engine
    // owns UI). Game code resizes any game-owned render targets here (e.g.
    // RenderSystem's offscreen FBO).
    using ResizeFn = void (*)(Engine&, int new_w, int new_h);
    void setOnResize(ResizeFn fn);

    // Set the window title string (for game-side HUD display).
    void setWindowTitle(const std::string& title);

    // Render ONE black frame with a "Loading..." overlay and swap the
    // buffers. Call from heavy init steps between window creation and
    // the main frame loop so the user sees a responsive window instead
    // of a frozen black rectangle. Pumps SDL events to keep the OS
    // from marking the window as unresponsive.
    void renderLoadingFrame(const char* status_text);

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

    // Background color used to clear the framebuffer each frame. Components are
    // 0..1. Defaults to dark grey; games override at startup if they want a
    // different palette base.
    void setClearColor(float r, float g, float b)
    {
        clear_r = r;
        clear_g = g;
        clear_b = b;
    }

    // EMA-smoothed frame time for FPS calculation.
    double lastFrameTime() const
    {
        return last_frame_time;
    }

    // Raw wall-clock frame time (no smoothing). Use this for animation /
    // visual-rate updates that must reflect actual elapsed time, not the
    // smoothed FPS estimate.
    double frameDt() const
    {
        return frame_dt;
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
    // The window is now some size: read it, resize the UI, tell the game. One definition, so a
    // resize and a display change cannot disagree about what "the window changed" means.
    void syncDrawableSize();
    void handleWindowEvent(const SDL_Event& event);
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
    RenderWorldFn render_world = nullptr;
    RenderDebugFn render_debug = nullptr;
    RenderUIFn render_ui = nullptr;
    RenderImGuiFn render_imgui = nullptr;
    ResizeFn on_resize = nullptr;
    float camera_zoom = 1.0f;
    bool timing_reset_pending = false;
    float clear_r = 0.1f;
    float clear_g = 0.1f;
    float clear_b = 0.1f;
    int msaa_samples = 0;
    WindowMode window_mode = WindowMode::Windowed;
};
