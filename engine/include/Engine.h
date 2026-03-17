#pragma once

#include "TextureManager.h"
#include "ecs/EntityManager.h"

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

    // Exposed so game code (main.cpp, future scene managers) can create
    // entities and attach components before calling run().
    EntityManager& entityManager()
    {
        return entity_manager;
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
    double last_frame_time_ = 1.0 / 60.0; // seconds; used for title-bar FPS display
};
