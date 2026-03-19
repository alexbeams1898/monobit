#pragma once

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

    // Set the window title string (for game-side HUD display).
    void setWindowTitle(const std::string& title);

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
    double last_frame_time = 1.0 / 60.0; // seconds; used for title-bar FPS display
    double frame_dt = 1.0 / 60.0;        // raw wall-clock frame time for animation

    GameUpdateFn game_update = nullptr;
};
