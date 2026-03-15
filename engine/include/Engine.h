#pragma once

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
        return entityManager_;
    }

  private:
    void processEvents();
    void update(double dt);
    void render();

    SDL_Window* window = nullptr;
    void* glContext = nullptr;
    bool running = false;
    int windowW_ = 0;
    int windowH_ = 0;

    EntityManager entityManager_;
};
